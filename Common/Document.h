#pragma once

#include "DeferredRelease.h"
#include "Executor.h"
#include "HistoryEntries.h"
#include "InputRevision.h"
#include "ResidencyChangeJournal.h"

#include "PixelBuffer.h"
#include "RecordRoot.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

inline constexpr int kMaxCanvasSide = 30'000;
inline constexpr int kMaxTransformSide = 300'000;
inline constexpr double kMaxTransformOrigin = 1'000'000.0;
inline constexpr int kMaxLayerPixels = 100'000'000;
inline constexpr int kMaxMaskPixels = 100'000'000;
inline constexpr int kMaxLayerCount = 10'000;
inline constexpr int kMaxNameUtf8Bytes = 16'384;
inline constexpr int kMaxGroupDepth = 64;
inline constexpr int kMaxLiveMaskPath = 256;
inline constexpr int kMinFormatVersion = 1;
inline constexpr int kMaxFormatVersion = 7;
inline constexpr int kCurrentFormatVersion = 7;
inline constexpr int kHistoryEntryLimit = 100;
inline constexpr std::uint64_t kHistoryRetainedByteLimit = 256ull * 1024 * 1024;
// A failed physical-memory query uses the legacy class default. Division before capping cannot overflow.
[[nodiscard]] constexpr std::uint64_t HistoryLimitForPhysicalMemory(std::uint64_t bytes) noexcept
{
    return bytes == 0 ? kHistoryRetainedByteLimit : std::min<std::uint64_t>(bytes / 4, 2ull * 1024 * 1024 * 1024);
}

enum class DocumentStatus
{
    Ok,
    Invalid,
    UnsupportedVersion,
    TooLarge,
};

struct Guid
{
    std::array<std::uint8_t, 16> bytes{};

    [[nodiscard]] static Guid Generate() noexcept;
    [[nodiscard]] static std::optional<Guid> Parse(std::string_view text) noexcept;
    [[nodiscard]] std::string ToUtf8() const;
    [[nodiscard]] bool operator==(const Guid&) const noexcept = default;
    [[nodiscard]] bool operator<(const Guid& other) const noexcept
    {
        return bytes < other.bytes;
    }
    [[nodiscard]] std::wstring ToString() const;
};

template <> struct std::hash<Guid>
{
    [[nodiscard]] std::size_t operator()(const Guid& id) const noexcept
    {
        std::size_t hash = 2166136261u;
        for (const std::uint8_t byte : id.bytes)
        {
            hash ^= byte;
            hash *= 16777619u;
        }
        return hash;
    }
};

struct Vec2
{
    double x = 0;
    double y = 0;
    [[nodiscard]] bool operator==(const Vec2&) const noexcept = default;
};

// A 2-D affine map in the row-vector convention of CGAffineTransform: x' = a*x + c*y + tx, y' = b*x + d*y + ty.
struct Affine2
{
    double a = 1;
    double b = 0;
    double c = 0;
    double d = 1;
    double tx = 0;
    double ty = 0;
    [[nodiscard]] static Affine2 Translation(double x, double y) noexcept
    {
        return Affine2{1, 0, 0, 1, x, y};
    }
    [[nodiscard]] static Affine2 Scale(double x, double y) noexcept
    {
        return Affine2{x, 0, 0, y, 0, 0};
    }
    [[nodiscard]] static Affine2 Rotation(double radians) noexcept;
    [[nodiscard]] Vec2 Apply(Vec2 point) const noexcept
    {
        return Vec2{a * point.x + c * point.y + tx, b * point.x + d * point.y + ty};
    }
    // This map, then `other` (CGAffineTransform.concatenating).
    [[nodiscard]] Affine2 Concat(const Affine2& other) const noexcept;
    [[nodiscard]] Affine2 Inverted() const noexcept;
    [[nodiscard]] bool operator==(const Affine2&) const noexcept = default;
};

enum class RasterFormat
{
    Bgra8888,
    Gray8,
};

class Raster;
struct RasterTile;

// Immutable pixels with shared storage. A raster is either contiguous, or sparse (C10): an unchanged base placed in
// its grid plus replacement tiles, as a painted layer leaves it. Sparse rasters share every untouched tile and the
// base with the raster they came from, and assemble contiguous bytes only when a consumer asks for Data().
class Raster
{
  public:
    // Brief process-wide gate for sparse resident-cache publication. Workers may wait while sampling history;
    // owners only try. Holding this gate never authorizes pixel work on the owner. A lease stays on its acquiring
    // thread and is released before callbacks, waits or transfer to a different thread.
    class ResidencyLease final
    {
      public:
        ResidencyLease() noexcept = default;
        ~ResidencyLease() noexcept;
        ResidencyLease(const ResidencyLease&) = delete;
        ResidencyLease& operator=(const ResidencyLease&) = delete;
        ResidencyLease(ResidencyLease&&) = delete;
        ResidencyLease& operator=(ResidencyLease&&) = delete;
        [[nodiscard]] bool Held() const noexcept;
        // One nonblocking attempt; reuses the scope owner, refuses recursion and never waits/allocates.
        [[nodiscard]] bool TryAcquire() noexcept;
        void Reset() noexcept;

      private:
        friend class Raster;
        explicit ResidencyLease(bool attempt, CancelToken cancellation = {}) noexcept;
        void CheckThread() const noexcept;
        std::thread::id _thread;
        bool _held = false;
    };
    [[nodiscard]] static ResidencyLease AcquireResidencyLease(CancelToken cancellation = {}) noexcept;
    [[nodiscard]] static ResidencyLease TryResidencyLease() noexcept; // owner, one attempt only
    [[nodiscard]] static std::uint64_t ResidencyEpoch() noexcept;
    static constexpr std::size_t kResidencyJournalCapacity = 4096;
    using ResidencyJournal = RasterResidencyJournal<kResidencyJournalCapacity>;
    // Copies numeric cache-growth events only while this same thread owns the exclusive residency lease.
    [[nodiscard]] static ResidencyJournal::CopyResult CopyResidencyChanges(std::uint64_t afterEpoch,
                                                                           std::span<RasterResidencyChange> output,
                                                                           std::size_t& count,
                                                                           CancelToken cancellation = {}) noexcept;

    // Bounded, allocation-free cache-growth notices. Start/Stop belong to startup/teardown, outside leases and
    // callbacks. Stop joins any in-flight notice before the borrowed context can die. A notice runs after both
    // cache locks release and may only publish readiness/wake; it must not reenter subscriptions or read pixels.
    // This is notification only: it cannot authorize an edit or account/trim history on a producer thread.
    class ResidencySubscription final
    {
      public:
        using Changed = void (*)(void*) noexcept;
        static constexpr std::size_t kCapacity = 8;
        ResidencySubscription() noexcept = default;
        ~ResidencySubscription() noexcept;
        ResidencySubscription(const ResidencySubscription&) = delete;
        ResidencySubscription& operator=(const ResidencySubscription&) = delete;
        [[nodiscard]] bool Start(Changed changed, void* context) noexcept;
        void Stop() noexcept;

      private:
        friend class Raster;
        Changed _changed = nullptr;
        void* _context = nullptr;
        bool _registered = false;
    };
    [[nodiscard]] static Raster Make(int width, int height, RasterFormat format);
    [[nodiscard]] static Raster Make(int width, int height, RasterFormat format, const std::uint8_t* pixels,
                                     std::size_t byteCount);
    // Publishes a completely initialized buffer without copying. Invalid input leaves ownership with the caller.
    [[nodiscard]] static Raster Adopt(int width, int height, RasterFormat format, PixelBuffer&& pixels);
    // Caller precreates lifetime under its complete admission. The immutable storage and its weak control
    // block retain it through actual worker destruction; no pixel copy, mutation or new memory admission.
    [[nodiscard]] static Raster Adopt(int width, int height, RasterFormat format, PixelBuffer&& pixels,
                                      const std::shared_ptr<const void>& lifetime);
    [[nodiscard]] std::weak_ptr<const void> WeakStorageForTest() const noexcept
    {
        return _storage;
    }
    [[nodiscard]] static Raster MakeThumbnail(const Raster& source, int maxSide = 96);
    // A sparse raster: `base` (contiguous; may be empty) with its top-left at (baseX, baseY) of the new grid, under
    // `tiles` (clipped to the grid, later tiles over earlier ones). Pixels no base or tile reaches read `fill`.
    [[nodiscard]] static Raster MakeSparse(int width, int height, RasterFormat format, const Raster& base, int baseX,
                                           int baseY, std::vector<RasterTile> tiles, std::uint8_t fill);
    // `source` (its top-left at (sourceX, sourceY) of a working grid) with `tiles` of that grid written over it,
    // cropped to the given rectangle of the grid. Older tiles are split around newer ones so the tile list stays
    // disjoint; the base and untouched tiles are shared, never copied. `fill` is what the crop reads beyond the
    // source; a sparse source keeps its own fill over its own extent (a painted hide-all mask stays hidden).
    [[nodiscard]] static Raster Replacing(const Raster& source, int sourceX, int sourceY, std::vector<RasterTile> tiles,
                                          int cropX, int cropY, int cropWidth, int cropHeight, std::uint8_t fill,
                                          CancelToken cancellation = {});
    // A contiguous copy of the region (pixels outside this raster read `fill`).
    [[nodiscard]] Raster Crop(int x, int y, int width, int height, std::uint8_t fill) const;
    // This raster resampled to `width` × `height` by the nearest pixel (grid pixel x reads x · Width() / width), as
    // a mask stretched over its layer is painted; itself when the size already matches, empty when out of range.
    [[nodiscard]] Raster Stretched(int width, int height) const;
    [[nodiscard]] bool operator==(const Raster& other) const noexcept
    {
        return _storage == other._storage;
    }
    [[nodiscard]] bool HasStorage() const noexcept
    {
        return static_cast<bool>(_storage);
    }
    [[nodiscard]] int Width() const noexcept
    {
        return _storage ? _storage->width : 0;
    }
    [[nodiscard]] int Height() const noexcept
    {
        return _storage ? _storage->height : 0;
    }
    [[nodiscard]] int Stride() const noexcept
    {
        return _storage ? _storage->stride : 0;
    }
    [[nodiscard]] RasterFormat Format() const noexcept
    {
        return _storage ? _storage->format : RasterFormat::Bgra8888;
    }
    [[nodiscard]] int Channels() const noexcept
    {
        return Format() == RasterFormat::Gray8 ? 1 : 4;
    }
    // Bytes this storage holds itself: contiguous pixels, or a sparse raster's assembled copy once it exists. The
    // base and the tiles are separate storages (ForEachStorage).
    [[nodiscard]] std::size_t ByteSize() const noexcept
    {
        return IsMaterialized()
                   ? static_cast<std::size_t>(_storage->stride) * static_cast<std::size_t>(_storage->height)
                   : 0;
    }
    [[nodiscard]] const void* Identity() const noexcept
    {
        return _storage.get();
    }
    [[nodiscard]] std::uint64_t StorageSerial() const noexcept
    {
        return _storage ? _storage->serial : 0;
    }
    [[nodiscard]] int PixelCount() const noexcept
    {
        return Width() * Height();
    }
    // Contiguous pixels. A sparse raster assembles them on the first call; nullptr when that allocation fails.
    [[nodiscard]] const std::uint8_t* Data() const noexcept;
    [[nodiscard]] bool IsSparse() const noexcept
    {
        return _storage && _storage->sparse;
    }
    // A sparse raster whose contiguous bytes were assembled (mouse-up must never do this).
    [[nodiscard]] bool IsMaterialized() const noexcept
    {
        return _storage && _storage->assembled.load(std::memory_order_acquire) != nullptr;
    }
    [[nodiscard]] Raster Base() const noexcept
    {
        return _storage ? Raster(_storage->base) : Raster();
    }
    [[nodiscard]] int BaseX() const noexcept
    {
        return _storage ? _storage->baseX : 0;
    }
    [[nodiscard]] int BaseY() const noexcept
    {
        return _storage ? _storage->baseY : 0;
    }
    [[nodiscard]] const std::vector<RasterTile>& Tiles() const noexcept;
    [[nodiscard]] std::uint8_t Fill() const noexcept
    {
        return _storage ? _storage->fill : 0;
    }
    // The region [x, x + width) × [y, y + height) of this raster written to `out` (`outStride` bytes per row),
    // without assembling a sparse raster; pixels outside it read `fill`.
    void ReadRegion(int x, int y, int width, int height, std::uint8_t* out, int outStride,
                    std::uint8_t fill) const noexcept;
    // Every storage this raster reaches: itself, then its base and tiles (retained-memory accounting).
    void ForEachStorage(const std::function<void(const Raster&)>& visit) const;
    // Construction-time conservative capacity of this complete owning graph, including a possible sparse
    // cache. Sharing may overcount; zero on a stored raster refuses overflow/depth >128. Reading never traverses.
    [[nodiscard]] std::uint64_t RetirementMemoryBytes() const noexcept
    {
        return _storage ? _storage->retirementBytes : 0;
    }
    // Construction-time bound for this storage alone: header, tile-container capacity and pixels, including
    // a future sparse assembly. Base/tile storages are excluded. Unique ownership indexes sum it once per
    // held identity. It remains available when repeated child occurrences overflow the complete graph bound.
    [[nodiscard]] std::uint64_t StorageRetirementMemoryBytes() const noexcept
    {
        return _storage ? _storage->selfRetirementBytes : 0;
    }
    // Producer-cached workspace for splitting old windows with replacements on one 256-pixel grid.
    // Includes result/vector growth, older headers and one piece/next scratch; no pixel allocation.
    // Empty/contiguous rasters need zero old-window workspace. Unknown/overflow refuses admission.
    [[nodiscard]] std::optional<std::uint64_t> GridReplacementWorkspaceBytes() const noexcept
    {
        return _storage ? _storage->gridReplacementWorkspaceBytes : std::optional<std::uint64_t>{0};
    }
    [[nodiscard]] std::uint64_t RetirementNodeCount() const noexcept
    {
        return _storage && _storage->retirementBytes ? _storage->graphNodes : 0;
    }

  private:
    static void NotifyResidencyChanged() noexcept;
    // The intrusive base outlives the last shared_ptr when large storage is handed to the worker release queue.
#pragma warning(push)
#pragma warning(disable : 4324) // the intrusive release node requires 16-byte storage alignment
    struct Storage : DeferredReleaseNode
    {
        explicit Storage(std::uint64_t identity) noexcept : serial(identity) {}
        const std::uint64_t serial;
        int width = 0;
        int height = 0;
        int stride = 0;
        RasterFormat format = RasterFormat::Bgra8888;
        // Written once under assemblyMutex, then published through assembled. Readers never inspect bytes.
        mutable std::vector<std::uint8_t> bytes;
        PixelBuffer adoptedBytes;
        mutable std::atomic<const std::uint8_t*> assembled{nullptr};
        mutable std::mutex assemblyMutex;
        bool sparse = false;
        std::uint8_t fill = 0;
        std::shared_ptr<const Storage> base;
        int baseX = 0;
        int baseY = 0;
        std::vector<RasterTile> tiles;
        std::uint64_t selfRetirementBytes = 0;
        std::uint64_t retirementBytes = 0;
        std::optional<std::uint64_t> gridReplacementWorkspaceBytes;
        std::uint64_t graphNodes = 1;
        unsigned int graphDepth = 0;
    };
#pragma warning(pop)
    struct FreeStorage final
    {
        void operator()(Storage* storage) const noexcept;
    };
#pragma warning(push)
#pragma warning(disable : 4324) // inherits the intrusive release node's alignment
    struct ChargedStorage final : Storage
    {
        explicit ChargedStorage(std::uint64_t identity) noexcept : Storage(identity) {}
        std::shared_ptr<const void> lifetime;
    };
#pragma warning(pop)
    struct FreeChargedStorage final
    {
        void operator()(ChargedStorage* storage) const noexcept;
    };
    [[nodiscard]] static std::shared_ptr<Storage> AllocateStorage();
    [[nodiscard]] static std::shared_ptr<Storage> AllocateChargedStorage(std::shared_ptr<const void> lifetime);
    static void CacheRetirementMemory(Storage& storage) noexcept;
    explicit Raster(std::shared_ptr<const Storage> storage) noexcept : _storage(std::move(storage)) {}
    static void Materialize(const Storage& storage) noexcept;
    std::shared_ptr<const Storage> _storage;

  public:
    Raster() = default;
};

// One replacement tile of a sparse raster: a window of `pixels` (contiguous, the raster's format) placed with its
// top-left at (x, y) in the grid. A window smaller than the pixels is how a split tile keeps sharing its storage.
struct RasterTile
{
    int x = 0;
    int y = 0;
    Raster pixels;
    int sourceX = 0; // window origin inside `pixels`
    int sourceY = 0;
    int width = 0; // 0: the whole of `pixels`
    int height = 0;
    [[nodiscard]] int Width() const noexcept
    {
        return width > 0 ? width : pixels.Width();
    }
    [[nodiscard]] int Height() const noexcept
    {
        return height > 0 ? height : pixels.Height();
    }
};

struct RasterAsset
{
    Raster image;
    Raster thumbnail;
    std::wstring name;
    [[nodiscard]] bool operator==(const RasterAsset& other) const noexcept
    {
        return image == other.image && thumbnail == other.thumbnail && name == other.name;
    }
};

enum class LayerSampling
{
    Nearest,
    Smooth,
    High,
};

struct LayerTransform
{
    Vec2 origin{};
    Vec2 size{1, 1};
    double rotation = 0;
    bool flipX = false;
    bool flipY = false;
    LayerSampling sampling = LayerSampling::High;

    [[nodiscard]] bool operator==(const LayerTransform&) const noexcept = default;
    [[nodiscard]] bool IsValid() const noexcept;
    [[nodiscard]] Vec2 Center() const noexcept
    {
        return Vec2{origin.x + size.x / 2.0, origin.y + size.y / 2.0};
    }
    [[nodiscard]] double Radians() const noexcept;
    // The unit square (0…1, y down) mapped where this transform places a layer; flips are not applied here.
    [[nodiscard]] Vec2 Point(Vec2 unit) const noexcept;
    [[nodiscard]] bool Contains(Vec2 point) const noexcept;
    // Corners in handle order: top-left, top-right, bottom-right, bottom-left.
    [[nodiscard]] std::array<Vec2, 4> Corners() const noexcept;
    // Width as a percentage of the `pixelSize` it places (100 % draws them 1:1).
    [[nodiscard]] double ScalePercent(Vec2 pixelSize) const noexcept;
    // Both sides set to `percent` of `pixelSize`, keeping the center, rotation and flips.
    [[nodiscard]] LayerTransform ScaledToPercent(double percent, Vec2 pixelSize) const noexcept;
    // Whole pixels and whole degrees: what dragging, scaling and rotating leave behind.
    [[nodiscard]] LayerTransform Rounded() const noexcept;
    // The unit square mapped onto the document, flips included.
    [[nodiscard]] Affine2 UnitToDocument() const noexcept;
    // A transform placing the unit square as `map` does (shear dropped), keeping this sampling.
    [[nodiscard]] LayerTransform Placing(const Affine2& map) const noexcept;
    // This placement carried along as a layer moves from `from` to `to`.
    [[nodiscard]] LayerTransform Following(const LayerTransform& from, const LayerTransform& to) const noexcept;
    // The same place on the document, whatever the sampling.
    [[nodiscard]] bool SamePlacement(const LayerTransform& other) const noexcept;
    // Mirrored across a vertical line at `axis` (horizontally) or a horizontal one.
    [[nodiscard]] LayerTransform Mirrored(bool horizontally, double axis) const noexcept;
    static constexpr std::array<Vec2, 8> kHandles{Vec2{0, 0}, Vec2{0.5, 0}, Vec2{1, 0}, Vec2{1, 0.5},
                                                  Vec2{1, 1}, Vec2{0.5, 1}, Vec2{0, 1}, Vec2{0, 0.5}};
};

enum class LayerBlendMode
{
    Normal,
    Multiply,
    Screen,
    Overlay,
    Darken,
    Lighten,
    Difference,
    ColorDodge,
    ColorBurn,
    Hue,
    Saturation,
    Color,
    Luminosity,
};

[[nodiscard]] constexpr int LayerBlendModeCount() noexcept
{
    return static_cast<int>(LayerBlendMode::Luminosity) + 1;
}

enum class AdjustmentKind
{
    Hsv,
    Levels,
    Curves,
    Exposure,
    GradientMap,
    Grain,
};

enum class LevelsChannel
{
    Rgb,
    Red,
    Green,
    Blue,
};

enum class ColorRange : std::uint8_t
{
    Master,
    Reds,
    Yellows,
    Greens,
    Cyans,
    Blues,
    Magentas,
    Count,
};

struct LevelRange
{
    double black = 0;
    double gamma = 1;
    double white = 255;
    double outputBlack = 0;
    double outputWhite = 255;
    [[nodiscard]] bool operator==(const LevelRange&) const noexcept = default;
    [[nodiscard]] LevelRange Normalized() const noexcept;
};

struct LevelsSettings
{
    LevelsChannel channel = LevelsChannel::Rgb;
    std::array<LevelRange, 4> ranges{};
    [[nodiscard]] bool operator==(const LevelsSettings&) const noexcept = default;
};

struct CurvePoint
{
    double x = 0;
    double y = 0;
    [[nodiscard]] bool operator==(const CurvePoint&) const noexcept = default;
};

struct CurvesSettings
{
    LevelsChannel channel = LevelsChannel::Rgb;
    std::array<std::vector<CurvePoint>, 4> channels{
        std::vector<CurvePoint>{{0, 0}, {255, 255}},
        std::vector<CurvePoint>{{0, 0}, {255, 255}},
        std::vector<CurvePoint>{{0, 0}, {255, 255}},
        std::vector<CurvePoint>{{0, 0}, {255, 255}},
    };
    [[nodiscard]] bool operator==(const CurvesSettings&) const noexcept = default;
    [[nodiscard]] bool IsValid() const noexcept;
};

struct AdjustmentColor
{
    double red = 0;
    double green = 0;
    double blue = 0;
    [[nodiscard]] bool operator==(const AdjustmentColor&) const noexcept = default;
    [[nodiscard]] bool IsValid() const noexcept
    {
        return std::isfinite(red) && std::isfinite(green) && std::isfinite(blue) && red >= 0 && red <= 1 &&
               green >= 0 && green <= 1 && blue >= 0 && blue <= 1;
    }
};

struct ExposureSettings
{
    double exposure = 0;
    double offset = 0;
    double gamma = 1;
    [[nodiscard]] bool operator==(const ExposureSettings&) const noexcept = default;
    [[nodiscard]] bool IsValid() const noexcept
    {
        return exposure >= -20 && exposure <= 20 && offset >= -0.5 && offset <= 0.5 && gamma >= 0.01 && gamma <= 9.99;
    }
};

struct GradientMapSettings
{
    AdjustmentColor shadows{0, 0, 0};
    AdjustmentColor highlights{1, 1, 1};
    bool reversed = false;
    [[nodiscard]] bool operator==(const GradientMapSettings&) const noexcept = default;
    [[nodiscard]] bool IsValid() const noexcept
    {
        return shadows.IsValid() && highlights.IsValid();
    }
};

struct GrainSettings
{
    double amount = 25;
    double size = 1.5;
    double roughness = 50;
    std::uint32_t seed = 0;
    [[nodiscard]] bool operator==(const GrainSettings&) const noexcept = default;
    [[nodiscard]] bool IsValid() const noexcept
    {
        return amount >= 0 && amount <= 100 && size >= 0.5 && size <= 20 && roughness >= 0 && roughness <= 100;
    }
};

struct RangeAdjustment
{
    double hue = 0;
    double saturation = 0;
    double lightness = 0;
    [[nodiscard]] bool operator==(const RangeAdjustment&) const noexcept = default;
};

struct HueBand
{
    double falloffStart = 0;
    double rangeStart = 0;
    double rangeEnd = 360;
    double falloffEnd = 360;
    [[nodiscard]] bool operator==(const HueBand&) const noexcept = default;
    [[nodiscard]] std::array<double, 4> Handles() const noexcept
    {
        return {falloffStart, rangeStart, rangeEnd, falloffEnd};
    }
};

struct HueSaturationSettings
{
    ColorRange range = ColorRange::Master;
    bool colorize = false;
    bool invertRange = false;
    std::array<RangeAdjustment, static_cast<std::size_t>(ColorRange::Count)> adjustments{};
    std::array<HueBand, static_cast<std::size_t>(ColorRange::Count)> bands = DefaultBands();
    [[nodiscard]] bool operator==(const HueSaturationSettings&) const noexcept = default;
    [[nodiscard]] static std::array<HueBand, static_cast<std::size_t>(ColorRange::Count)> DefaultBands() noexcept;
};

struct LayerAdjustment
{
    AdjustmentKind kind = AdjustmentKind::Hsv;
    double hue = 0;
    double saturation = 0;
    double lightness = 0;
    bool colorize = false;
    std::optional<HueSaturationSettings> hsvSettings;
    LevelsSettings levels{};
    CurvesSettings curves{};
    std::optional<ExposureSettings> exposureSettings;
    std::optional<GradientMapSettings> gradientMapSettings;
    std::optional<GrainSettings> grainSettings;
    [[nodiscard]] bool operator==(const LayerAdjustment&) const noexcept = default;
    [[nodiscard]] HueSaturationSettings ResolvedHsv() const noexcept;
    [[nodiscard]] ExposureSettings Exposure() const noexcept
    {
        return exposureSettings.value_or(ExposureSettings{});
    }
    [[nodiscard]] GradientMapSettings GradientMap() const noexcept
    {
        return gradientMapSettings.value_or(GradientMapSettings{});
    }
    [[nodiscard]] GrainSettings Grain() const noexcept
    {
        return grainSettings.value_or(GrainSettings{});
    }
    [[nodiscard]] bool IsValid() const noexcept;
};

// Immutable CPU lookup values prepared off the UI thread. Graphics owners upload them on their own device.
struct AdjustmentLookup final
{
    LayerAdjustment adjustment;
    std::array<float, 256 * 4> table{};
    std::vector<float> cube;
};

enum class ShapeKind
{
    Rectangle,
    Ellipse,
};

[[nodiscard]] const char* LayerBlendModeName(LayerBlendMode mode) noexcept;
[[nodiscard]] std::optional<LayerBlendMode> LayerBlendModeFromName(std::string_view name) noexcept;
[[nodiscard]] const char* LayerSamplingName(LayerSampling sampling) noexcept;
[[nodiscard]] std::optional<LayerSampling> LayerSamplingFromName(std::string_view name) noexcept;
[[nodiscard]] const char* AdjustmentKindName(AdjustmentKind kind) noexcept;
[[nodiscard]] std::optional<AdjustmentKind> AdjustmentKindFromName(std::string_view name) noexcept;
[[nodiscard]] const char* LevelsChannelName(LevelsChannel channel) noexcept;
[[nodiscard]] std::optional<LevelsChannel> LevelsChannelFromName(std::string_view name) noexcept;
[[nodiscard]] const char* ColorRangeName(ColorRange range) noexcept;
[[nodiscard]] std::optional<ColorRange> ColorRangeFromName(std::string_view name) noexcept;
[[nodiscard]] const char* ShapeKindName(ShapeKind kind) noexcept;
[[nodiscard]] std::optional<ShapeKind> ShapeKindFromName(std::string_view name) noexcept;

struct LayerShapeStyle
{
    ShapeKind kind = ShapeKind::Rectangle;
    double red = 0;
    double green = 0;
    double blue = 0;
    double cornerRadius = 0;
    [[nodiscard]] bool operator==(const LayerShapeStyle&) const noexcept = default;
};

struct LayerMask
{
    RasterAsset asset;
    bool enabled = true;
    std::optional<LayerTransform> placement;
    bool linked = true;
    [[nodiscard]] bool operator==(const LayerMask& other) const noexcept
    {
        return asset.image == other.asset.image && enabled == other.enabled && placement == other.placement &&
               linked == other.linked;
    }
    [[nodiscard]] static bool IsValid(const Raster& image) noexcept;
};

// Where a mask sits after its layer moves from `from` to `to`: a linked placement follows, an unlinked mask stays,
// a uniform 1 x 1 mask and a mask that lands exactly on its layer report nullopt.
[[nodiscard]] std::optional<LayerTransform> MaskPlacementMoving(const LayerMask& mask, const LayerTransform& from,
                                                                const LayerTransform& to) noexcept;

// Selection (C9, Specs/Document/Document_Tools.md): closed loops in document pixels under the nonzero winding rule,
// Clipper2-normalized (outer loops and holes) except a wand or mask trace, whose pixel-edge loops already are.
using SelectionLoop = std::vector<Vec2>;

struct SelectionBounds
{
    double minX = 0;
    double minY = 0;
    double maxX = 0;
    double maxY = 0;
    [[nodiscard]] double Width() const noexcept
    {
        return maxX - minX;
    }
    [[nodiscard]] double Height() const noexcept
    {
        return maxY - minY;
    }
    [[nodiscard]] bool operator==(const SelectionBounds&) const noexcept = default;
};

// The bounding box of `points`: each axis's min and max over every point (the first point's, alone, when there is
// just one). `points` must not be empty.
[[nodiscard]] SelectionBounds PointBounds(std::span<const Vec2> points) noexcept;
// The bounding box of every finite point across `loops` (corners, wand traces and Clipper2 results are always
// finite; a lasso or transform draft is not), skipping the rest; nullopt when none are finite.
[[nodiscard]] std::optional<SelectionBounds> LoopsBounds(const std::vector<SelectionLoop>& loops) noexcept;

// Published selection geometry is immutable. Copies of documents, history and job inputs share the points;
// editing an outline constructs a new value. Equality still compares geometry, so no-op edits preserve redo.
class SelectionLoops
{
  public:
    SelectionLoops() noexcept = default;
    // Worker-only immutable adoption: cancellable cached metadata, no point-buffer copy. The caller
    // admits storage/control bounds and explicitly retains its output-phase lifetime when charged.
    // Last owner-thread destruction of geometry is deferred; last weak control retains accounting.
    [[nodiscard]] static std::optional<SelectionLoops> AdoptForWork(
        std::vector<SelectionLoop> loops, CancelToken cancellation = {}, std::shared_ptr<const void> lifetime = {},
        void (*checkpoint)(void*, std::uint64_t) noexcept = nullptr, void* context = nullptr);
    [[nodiscard]] static std::uint64_t WorkOwnershipAllowance(std::uint64_t loopCount) noexcept;
    [[nodiscard]] std::weak_ptr<const void> WeakStorageForTest() const noexcept
    {
        return _loops;
    }

    SelectionLoops(std::initializer_list<SelectionLoop> loops) : SelectionLoops(std::vector<SelectionLoop>(loops)) {}
    SelectionLoops(std::vector<SelectionLoop> loops)
        : _loops(loops.empty() ? nullptr : std::make_shared<const std::vector<SelectionLoop>>(std::move(loops)))
    {
        if (_loops)
        {
            _storageBytes = sizeof(std::vector<SelectionLoop>) + Get().capacity() * sizeof(SelectionLoop);
        }
        for (const SelectionLoop& loop : Get())
        {
            _pointCount += loop.size(); // Owned Vec2 allocations cannot together exceed addressable bytes.
            _storageBytes += loop.capacity() * sizeof(Vec2);
        }
        _bounds = LoopsBounds(Get());
    }
    SelectionLoops(const SelectionLoops&) noexcept = default;
    SelectionLoops& operator=(const SelectionLoops&) noexcept = default;
    SelectionLoops(SelectionLoops&& other) noexcept
        : _loops(std::move(other._loops)), _pointCount(std::exchange(other._pointCount, 0)),
          _bounds(std::exchange(other._bounds, std::nullopt)), _storageBytes(std::exchange(other._storageBytes, 0))
    {
    }
    SelectionLoops& operator=(SelectionLoops&& other) noexcept
    {
        if (this != &other)
        {
            _loops = std::move(other._loops);
            _pointCount = std::exchange(other._pointCount, 0);
            _bounds = std::exchange(other._bounds, std::nullopt);
            _storageBytes = std::exchange(other._storageBytes, 0);
        }
        return *this;
    }
    [[nodiscard]] const std::vector<SelectionLoop>& Get() const noexcept
    {
        static const std::vector<SelectionLoop> empty;
        return _loops ? *_loops : empty;
    }
    [[nodiscard]] operator const std::vector<SelectionLoop>&() const noexcept
    {
        return Get();
    }
    [[nodiscard]] bool operator==(const SelectionLoops& other) const noexcept
    {
        return _loops == other._loops || Get() == other.Get();
    }
    [[nodiscard]] const void* Identity() const noexcept
    {
        return _loops.get();
    }
    [[nodiscard]] auto begin() const noexcept
    {
        return Get().begin();
    }
    [[nodiscard]] auto end() const noexcept
    {
        return Get().end();
    }
    [[nodiscard]] bool empty() const noexcept
    {
        return Get().empty();
    }
    [[nodiscard]] std::size_t size() const noexcept
    {
        return Get().size();
    }
    // Computed by the geometry producer. Admission and snapshot readers never traverse the outline.
    [[nodiscard]] std::uint64_t PointCount() const noexcept
    {
        return _loops ? _pointCount : 0;
    }
    // Computed with the immutable geometry so selection bounds readers never traverse the outline.
    [[nodiscard]] std::optional<SelectionBounds> Bounds() const noexcept
    {
        return _loops ? _bounds : std::nullopt;
    }
    // Declared retained storage: the shared outer vector header and its retained capacity, plus each loop's retained
    // capacity. Excludes allocator and shared-control-block overhead; reads the construction-time cache only.
    [[nodiscard]] std::uint64_t StorageBytes() const noexcept
    {
        return _loops ? _storageBytes : 0;
    }
    [[nodiscard]] const SelectionLoop& operator[](std::size_t index) const noexcept
    {
        return Get()[index];
    }

  private:
    friend class SelectionNudgeBuilder;
    // Trusted worker producer only: cached metadata was accumulated while constructing these exact loops.
    // Reuse normal GeometryNode/weak-control lifetime without a second complete geometry traversal.
    [[nodiscard]] bool SealPreparedStorageForWork(std::vector<SelectionLoop> loops,
                                                  std::shared_ptr<const void> lifetime);

    std::shared_ptr<const std::vector<SelectionLoop>> _loops;
    std::uint64_t _pointCount = 0;
    std::optional<SelectionBounds> _bounds;
    std::uint64_t _storageBytes = 0;
};

// A document-space selection outline, clipped to the canvas when drawn (not when moved). nullopt on the document
// means no selection; a selection with no area is an explicit empty selection, which later edits must treat as
// "touch nothing", never as "touch everything".
struct DocumentSelection
{
    bool antialiased = true;
    SelectionLoops loops;
    [[nodiscard]] bool operator==(const DocumentSelection&) const noexcept = default;
    [[nodiscard]] std::optional<SelectionBounds> Bounds() const noexcept
    {
        return loops.Bounds();
    }
    [[nodiscard]] bool IsEmpty() const noexcept
    {
        const std::optional<SelectionBounds> bounds = Bounds();
        return !bounds.has_value() || !(bounds->Width() > 0.0) || !(bounds->Height() > 0.0);
    }
};

// Overlay readers share immutable geometry and carry pixel/outline-move offsets as bounded scalar metadata.
// The numeric DisplayedSelection reference still returns translated geometry.
struct SelectionDisplayValue final
{
    std::optional<DocumentSelection> selection;
    Vec2 translation{};
};

enum class LassoKind : std::uint8_t
{
    Freehand,
    Polygonal,
    // The Marquee's outlines; not offered in the Lasso's Freehand/Polygonal choice.
    Rectangle,
    Ellipse,
};

enum class SelectionMode : std::uint8_t
{
    New,
    Add,
    Subtract,
};

// A lasso outline being drawn, in document pixels. `cursor` is the polygonal lasso's rubber-band end point;
// `anchor` the Marquee's starting corner, in whole pixels.
class BoundedLassoGesture;
struct LassoOpeningSettings;
class LassoOpeningInput;
class PreparedLassoOpening;

struct LassoDraft
{
    LassoDraft() = default;
    LassoDraft(std::vector<Vec2> value, std::optional<Vec2> at, SelectionMode selectionMode, LassoKind lasso,
               std::optional<Vec2> start)
        : points(std::move(value)), cursor(at), mode(selectionMode), kind(lasso), anchor(start)
    {
    }
    [[nodiscard]] std::size_t PointCount() const noexcept;
    [[nodiscard]] std::optional<Vec2> FirstPoint() const noexcept;
    [[nodiscard]] std::uint64_t RetirementMemoryBytes() const noexcept;
    [[nodiscard]] bool IsBounded() const noexcept
    {
        return static_cast<bool>(_bounded);
    }
    // Reference-only geometry. An admitted draft leaves this vector empty; owner readers use scalar accessors.
    std::vector<Vec2> points;
    std::optional<Vec2> cursor;
    SelectionMode mode = SelectionMode::New;
    LassoKind kind = LassoKind::Freehand;
    std::optional<Vec2> anchor;

  private:
    friend class DocumentSession;
    std::shared_ptr<BoundedLassoGesture> _bounded;
    std::uint64_t _boundedRetirementBytes = 0;
};

enum class WandSampleSize : std::uint8_t
{
    Point,
    ThreeByThree,
    FiveByFive,
};

// The Magic Wand's header settings.
struct WandSettings
{
    // How far (0–255) each channel may differ from the sampled color and still be selected.
    int tolerance = 32;
    WandSampleSize sampleSize = WandSampleSize::Point;
    // Only similar pixels connected to the clicked one, rather than every similar pixel.
    bool contiguous = true;
    // Read the visible composite rather than just the active layer.
    bool sampleAllLayers = false;
    // Pixels either side of the click that are averaged into the color to match.
    [[nodiscard]] int Radius() const noexcept
    {
        return static_cast<int>(sampleSize);
    }
    [[nodiscard]] bool operator==(const WandSettings&) const noexcept = default;
};

enum class WandFailure : std::uint8_t
{
    None,
    TooDetailed,
    Memory,
    Cancelled,
};

struct ImageLayer
{
    Guid id;
    std::optional<RasterAsset> asset;
    LayerTransform transform{};
    std::wstring name;
    bool visible = true;
    std::optional<Guid> parentID;
    bool isGroup = false;
    double opacity = 1;
    LayerBlendMode blendMode = LayerBlendMode::Normal;
    std::optional<Guid> maskSourceID;
    std::optional<LayerMask> mask;
    std::optional<LayerAdjustment> adjustment;
    std::optional<LayerShapeStyle> shape;
    // The raster the shape drew (C11): while `asset` still holds it the layer is a live shape that redraws at a new
    // size; once anything else changes the pixels it is plain pixels. Not part of equality.
    Raster shapeImage;

    [[nodiscard]] bool operator==(const ImageLayer& other) const noexcept;
    static ImageLayer Blank(std::wstring name, Vec2 size);
};

template <> struct RecordSavePolicy<ImageLayer> final
{
    static constexpr bool enabled = true;
    [[nodiscard]] static bool Accumulate(RecordSavePlan& plan, const ImageLayer& layer) noexcept;
};

template <> struct RecordOwnershipPolicy<ImageLayer> final
{
    static constexpr bool enabled = true;
    [[nodiscard]] static std::optional<RecordOwnershipBounds> Measure(const ImageLayer& layer) noexcept;
};

template <> struct RecordLookupPolicy<ImageLayer> final
{
    static constexpr bool enabled = true;
    static constexpr std::size_t maximumRecords = kMaxLayerCount;
    // Validation and malformed-input fixtures retain oversized record values.
    // They receive no index/uniqueness authority and cannot pass document admission.
    static constexpr bool allowUnindexedOverflow = true;
    using Key = Guid;
    [[nodiscard]] static Guid KeyOf(const ImageLayer& layer) noexcept
    {
        return layer.id;
    }
};

struct CanvasDocument
{
    Guid id;
    int width = 1;
    int height = 1;
    double resolution = 72;
    RecordRoot<ImageLayer> layers;
    std::optional<DocumentSelection> selection;
    [[nodiscard]] bool operator==(const CanvasDocument&) const noexcept = default;
    // Complete conservative CPU ownership of this source only, using immutable layer/selection caches.
    // Includes potential sparse assembly; sharing may overcount. No traversal/allocation/materialization.
    // Zero refuses an unavailable child bound or overflow; a valid default empty root has a nonzero bound.
    [[nodiscard]] std::uint64_t RetirementMemoryBytes() const noexcept;
};

// Result tokens own their source handles: a freed storage address can never alias a new image. The complete
// mask record matters because placement, linking and enablement affect pixels as well as its raster does.
struct TargetIdentity
{
    Guid layerId;
    Raster image;
    LayerTransform transform;
    std::optional<LayerMask> mask;
    bool isGroup = false;
    std::optional<LayerAdjustment> adjustment;
};

struct JobToken
{
    Guid sessionIdentity;
    std::uint64_t documentSerial = 0;
    std::uint64_t generation = 0;
    std::uint64_t restoreSerial = 0;
    std::optional<TargetIdentity> target;
    std::optional<DocumentSelection> selection;
    int canvasWidth = 0;
    int canvasHeight = 0;
    std::uint64_t sequence = 0;
};

// A descriptor-only read authority: no layer, raster or selection owner is captured before admission.
struct DocumentReadAuthority final
{
    Guid sessionIdentity;
    std::uint64_t documentSerial = 0;
    std::uint64_t generation = 0;
    std::uint64_t restoreSerial = 0;
    int canvasWidth = 0;
    int canvasHeight = 0;
    bool operator==(const DocumentReadAuthority&) const noexcept = default;
};

// The Crop tool's frame in document pixels (C16, Common/Crop.h holds its geometry, drag and snap).
struct CropRect
{
    double x = 0;
    double y = 0;
    double width = 0;
    double height = 0;

    [[nodiscard]] double MinX() const noexcept
    {
        return x;
    }
    [[nodiscard]] double MinY() const noexcept
    {
        return y;
    }
    [[nodiscard]] double MaxX() const noexcept
    {
        return x + width;
    }
    [[nodiscard]] double MaxY() const noexcept
    {
        return y + height;
    }
    [[nodiscard]] double MidX() const noexcept
    {
        return x + width / 2.0;
    }
    [[nodiscard]] double MidY() const noexcept
    {
        return y + height / 2.0;
    }
    [[nodiscard]] CropRect OffsetBy(double dx, double dy) const noexcept
    {
        return CropRect{x + dx, y + dy, width, height};
    }
    [[nodiscard]] bool operator==(const CropRect&) const noexcept = default;
};

// Several layers transformed together: the upright box around them when the edit began (what the draft edits) and
// each one's transform then.
struct SnapTargets;

struct TransformGroup
{
    LayerTransform box;
    std::vector<std::pair<Guid, LayerTransform>> originals;
};

// Ctrl+T with a selection (C9b): the selected pixels float on a temporary layer, edited with the normal transform
// handles, then merge back into their layer as one `Transform Selection` entry; Escape restores exactly.
struct FloatingTransform
{
    Guid sourceID;
    std::shared_ptr<CanvasDocument> before;
    std::optional<Guid> beforeActive;
    LayerTransform original;
    int pixelWidth = 0;
    int pixelHeight = 0;
};

// A pending transform: the draft the handles and inspector edit. `persistent` edits wait for Apply/Cancel; others
// commit when the drag ends. `corners` set means a free distortion; `mask` an unlinked mask placed alone.
struct TransformEdit
{
    Guid layerID;
    LayerTransform draft;
    bool persistent = true;
    std::optional<std::array<Vec2, 4>> corners;
    bool mask = false;
    std::optional<TransformGroup> group;
    // Set when transforming selected pixels (Ctrl+T with a selection) rather than a layer.
    std::optional<FloatingTransform> floating;
    bool deferred = false;
    std::uint64_t opening = 0; // Stable across settings ticks; assigned when owner policy becomes deferred.
};

// Document positions a move has just snapped to, drawn as guides while it lasts.
struct SnapGuides
{
    std::vector<double> xs;
    std::vector<double> ys;
};

// Brush (C10, Specs/Rendering/Rendering_Brush.md).
// Spot Healing's kind of repair (C11, Specs/Document/Document_Tools.md; the legacy HealPixels.c kernel, `Heal`).
enum class SpotHealingMode : std::uint8_t
{
    // Copies texture from the nearby patch whose surrounding ring of pixels best matches the ring around the spot.
    ContentAware,
    // Fills smoothly from the spot's edges and adds grain matching the detail around it.
    CreateTexture,
    // Like ContentAware, but takes the closest good patch.
    ProximityMatch,
};

// The Smear tool's modes: Liquify pushes pixels, Blur softens, Smudge drags color along.
enum class SmearMode : std::uint8_t
{
    Liquify,
    Blur,
    Smudge,
};

// What a brush stroke does to the pixels under the tip.
enum class BrushKind : std::uint8_t
{
    Paint,
    Erase,
    Heal,
    Clone,
    Blur,
};

// Clone Stamp's options-bar settings.
struct CloneSettings
{
    // The source moves with the brush and keeps its offset between strokes; off, every stroke starts again at the
    // source point.
    bool aligned = true;
    // Copy from every visible layer as shown rather than the active layer alone.
    bool sampleAllLayers = false;
    [[nodiscard]] bool operator==(const CloneSettings&) const noexcept = default;
};

// Linear runs from start to end; radial is centered on the start with the end on its rim.
enum class GradientShape : std::uint8_t
{
    Linear,
    Radial,
};

enum class GradientStyle : std::uint8_t
{
    ForegroundToBackground,
    ForegroundToTransparent,
};

struct GradientSettings
{
    GradientShape shape = GradientShape::Linear;
    GradientStyle style = GradientStyle::ForegroundToTransparent;
    bool reversed = false;
    double opacity = 1;
    [[nodiscard]] bool operator==(const GradientSettings&) const noexcept = default;
};

struct BrushSettings
{
    double diameter = 40; // document pixels, 1–2000
    double hardness = 1;  // 0 (soft) … 1 (hard)
    double red = 0;       // paint color, straight 0…1
    double green = 0;
    double blue = 0;
    // Caps the whole stroke, as in Photoshop: overlapping dabs never exceed it (0.01–1).
    double opacity = 1;
    [[nodiscard]] bool operator==(const BrushSettings&) const noexcept = default;
    [[nodiscard]] bool IsValid() const noexcept;
    // Soft-tip deposition rate shared by the continuous integral and the dab fallback.
    [[nodiscard]] static double SpacingFraction(double hardness) noexcept
    {
        return hardness >= 1.0 ? 0.015 : 0.025;
    }
};

enum class BrushToolMode : std::uint8_t
{
    Paint,
    Erase,
};

class BrushStroke;
class PreparedRasterEdit;
enum class ClipboardCommandKind : std::uint8_t;
class ClipboardCommandInput;
struct CopyPixelsInput;
class PreparedClipboardCommand;
class ClipboardPasteInput;
class PreparedClipboardPaste;
struct GridRect;
struct PreparedRasterPreview;
class BrushCoverageEngine;
class WarpStroke;

// An uncommitted gradient on one layer or mask (C11). Endpoints are document pixels; the raster preview lives in the
// stroke's tiles and never touches the document until commit.
struct GradientEdit
{
    struct CapturedInputs final
    {
        GradientSettings settings;
        std::uint32_t foreground;
        std::uint32_t background;
        bool maskWhite;
        [[nodiscard]] bool operator==(const CapturedInputs&) const noexcept = default;
    };
    std::shared_ptr<const PreparedRasterPreview> display;
    std::shared_ptr<BrushStroke> raster;
    Vec2 start;
    Vec2 end;
    Guid target;
    bool mask = false;
    bool deferred = false;
    std::uint64_t opening = 0;
    std::uint64_t sequence = 1;
    std::uint64_t publishedSequence = 0;
    std::uint64_t publishedGeneration = 0;
    std::optional<CapturedInputs> capturedInputs; // Event-time values of an admitted, delayed pointer opening.
    [[nodiscard]] bool HasLine() const noexcept
    {
        return std::hypot(end.x - start.x, end.y - start.y) >= 0.5;
    }
};

// A shape being dragged out with the Shape tool, in whole document pixels.
struct ShapeDraft
{
    ShapeKind kind = ShapeKind::Rectangle;
    Vec2 anchor;
    SelectionBounds rect;
    // Document pixels, fixed when the drag starts; rectangles only.
    double cornerRadius = 0;
    [[nodiscard]] bool operator==(const ShapeDraft&) const noexcept = default;
};

// The shape this layer still is: nullopt once its pixels were edited some other way.
[[nodiscard]] std::optional<LayerShapeStyle> LiveShape(const ImageLayer& layer) noexcept;

// Fill with the foreground or background color (C9b).
enum class FillSource : std::uint8_t
{
    Foreground,
    Background,
};

// Selected pixels being dragged (C9b): the lifted raster plus the outline it started from.
struct PixelMove
{
    std::shared_ptr<const PreparedRasterPreview> display;
    std::shared_ptr<BrushStroke> raster;
    DocumentSelection origin;
    bool duplicate = false;
    Vec2 offset;
    Guid target;
    bool deferred = false;
    std::uint64_t opening = 0;
    std::uint64_t sequence = 1;
    std::uint64_t publishedSequence = 0;
    std::uint64_t publishedGeneration = 0;
    std::uint64_t openingGeneration = 0;
    [[nodiscard]] bool HasOffset() const noexcept
    {
        return offset.x != 0 || offset.y != 0;
    }
};

// Pixels copied from the canvas, with where they came from so Paste can put them back in place.
struct PixelClipboard
{
    Raster image;
    Vec2 origin;
};

// ── Color adjustments and filters (C12) ───────────────────────────────────

// Every panel that changes a layer's pixels: the Filter menu's filters and the Image menu's color adjustments
// (Curves onward, each also an adjustment layer kind). Each runs on the active image layer, inside the selection if
// there is one, with a live preview and one undo step on OK. Remove Background (C13) is not in v1.
enum class FilterKind : std::uint8_t
{
    GaussianBlur,
    MotionBlur,
    AddNoise,
    LensCorrection,
    ContentAwareFill, // C13: the selection filled from its surroundings (automatic, no settings)
    Curves,
    Exposure,
    GradientMap,
    Grain,
    Levels,
    HueSaturation,
};

// Every kind's settings; each kind reads only its own.
struct FilterSettings
{
    double radius = 1;          // Gaussian Blur: the blur's standard deviation in layer pixels, 0.1–250
    double angle = 0;           // Motion Blur: degrees, counterclockwise from horizontal as in Photoshop, −90–90
    double distance = 10;       // Motion Blur: streak length in layer pixels, 1–2000
    double amount = 10;         // Add Noise: Photoshop's percentage, 0.1–400
    bool gaussian = false;      // Add Noise: Gaussian (more speckled) instead of Uniform
    bool monochromatic = false; // Add Noise: the same amount on every channel
    double distortion = 0;      // Lens Correction: Remove Distortion −100–100, positive straightens barrel distortion
    CurvesSettings curves{};
    ExposureSettings exposure{};
    GradientMapSettings gradientMap{};
    GrainSettings grain{};
    LevelsSettings levels{};
    HueSaturationSettings hsv{};
    [[nodiscard]] bool operator==(const FilterSettings&) const noexcept = default;
    [[nodiscard]] FilterSettings Normalized() const;
};

enum class LevelsSample : std::uint8_t
{
    Black,
    Gray,
    White,
};

enum class LevelsAuto : std::uint8_t
{
    Contrast,
    Color,
    Neutral, // Color + neutral midtones
};

// Which eyedropper is armed while the Hue/Saturation panel is open.
enum class HueSampleMode : std::uint8_t
{
    Replace,
    Add,
    Remove,
};

// RGB (the mean of the three channel histograms) followed by red, green and blue; alpha-weighted.
using LevelsHistogram = std::array<std::array<double, 256>, 4>;

// A targeted-adjustment drag in progress: the range it picked and the values it started from.
struct HueTargetDrag
{
    ColorRange range = ColorRange::Reds;
    double hue = 0;
    double saturation = 0;
};

enum class SelectionCommandKind : std::uint8_t;
class SelectionCommandInput;
class SelectionCommandAuthority;
class PreparedSelectionCommand;
enum class LayerMetadataKind : std::uint8_t;
class LayerMetadataAuthority;
class LayerMetadataInput;
class PreparedLayerMetadata;
struct LayerMetadataPrepareObserver;
class SelectionNudgeInput;
class PreparedSelectionNudge;
struct SelectionNudgeObserver;
class InverseSelectionInput;
class PreparedInverseSelection;
class SelectionOffsetInput;
class PreparedSelectionOffset;
struct SelectionOffsetMemoryPlan;
struct SelectionOffsetObserver;
struct InverseSelectionMemoryPlan;
struct InverseSelectionObserver;
class LassoFinishInput;
class PreparedLassoFinish;
struct LassoFinishTerminal;
struct LassoFinishObserver;
class WandLayerSelectionInput;
class PanelWandAuthority;
class PanelWandOpening;
class PreparedWandLayerSelection;
struct WandLayerSelectionObserver;
struct WandLayerSelectionMemoryPlan;
class SelectionMoveOpeningInput;
class PreparedSelectionMoveOpening;
class SelectionMoveClosureInput;
class PreparedSelectionMoveClosure;
struct ColorEditJobInput;
struct ColorEditJobResult;
enum class ColorEditJobStatus;
class ColorDisplayPreviewInput;
class PreparedColorDisplayPreview;
struct ColorDisplayPreviewMemory;
struct ColorDisplayPreviewResult;
struct ColorDisplayObserver;
class PreparedColorEdit;
class PreparedDocumentSize;
class PreparedHistoryRestore;
class PreparedImageImport;
struct AdjustmentClosureInput;
class PreparedAdjustmentClosure;
struct AdjustmentOpeningInput;
class PreparedAdjustmentOpening;
struct ColorDismissalInput;
class PreparedColorDismissal;
class ImageImportAuthority;
class ShapeEditInput;
class PreparedShapeEdit;
class TransformCommitInput;
class TransformPreviewInput;
class PreparedTransformPreview;
struct PendingDisplaySource;
struct TransformPreviewResult;
struct TransformPreviewObserver;
class TransformCancellationInput;
class PreparedTransformCancellation;
class DuplicateOpeningInput;
class PreparedDuplicateOpening;
class PreparedDuplicateRestore;
struct DuplicateOpeningResult;
struct DuplicateOpeningObserver;
class FloatingOpeningInput;
class PreparedFloatingOpening;
class PreparedFloatingRestore;
class FloatingMergeInput;
class PreparedFloatingMerge;
struct FloatingMergeResult;
struct FloatingMergeObserver;
struct FloatingOpeningResult;
struct FloatingOpeningObserver;
class PreparedTransformCommit;
struct TransformCommitResult;
struct ShapeEditResult;
struct ShapeWorkObserver;
class MaskEditInput;
class PreparedMaskEdit;
struct MaskEditResult;
struct MaskWorkObserver;
class LayerCopyInput;
class PreparedLayerCopy;
class SharedPublicationMemory;
class WarpSampleInput;
class WarpRetirementInput;
class WarpFinishInput;
struct WarpFinishMemoryPlan;
struct WarpSampleMemoryPlan;
class WarpOpeningInput;
class PreparedWarpOpening;
struct WarpOpeningOptions;
struct WarpOpeningResult;
struct WarpOpeningObserver;
class StrokeOpeningInput;
class PreparedStrokeOpening;
struct StrokeOpeningOptions;
struct StrokeOpeningResult;
struct StrokeOpeningObserver;
class StrokeFinishInput;
class StrokeSampleInput;
class PreparedStrokeFinish;
struct StrokeFinishResult;
struct StrokeFinishObserver;
struct LayerCopyResult;
struct LayerCopyObserver;
struct ImageImportBatchResult;
class HistoryRestoreAuthority;
struct ColorEditPreviewResult;
struct ColorEditHistogramResult;

struct ColorEditGrid final
{
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    [[nodiscard]] bool operator==(const ColorEditGrid&) const noexcept = default;
};

// The one open color or filter panel, whatever its kind: the layer's pixels it started from and where they sit, the
// selection it is limited to, the grid grown for a blur or a fill to spread into, the copy the preview renders from
// and what the canvas shows meanwhile (`previewRaster`, or worker lookup/source/coverage for a GPU preview).
// With neither representation the layer's own pixels show. An adjustment layer's
// panel holds the composite beneath the layer as `original`, for the histogram and sampling only.
struct ColorEdit
{
    FilterKind kind = FilterKind::GaussianBlur;
    Guid layerID;
    Raster original;
    LayerTransform transform;
    std::optional<DocumentSelection> selection;
    FilterSettings settings;
    std::optional<JobToken> sourceToken; // Opening inputs; absent for adjustment-layer panels.
    bool deferred = false; // Host captures metadata; its jobs run the same graph as direct inline callers.
    std::uint64_t openingSequence = 0;
    std::uint64_t previewSequence = 0;            // Exact settings/seed/grid sequence of the published preview.
    std::uint64_t previewInvalidatedSequence = 0; // Preview off/identity/growth refusal invalidates earlier output.
    bool commitFailed = false;
    bool preview = true;
    std::uint32_t seed = 0;   // Add Noise's and Grain's pattern, fixed while the panel is open
    Raster grownImage;        // the pixels padded out for a blur or a fill (empty when not grown)
    Raster retiredGrownImage; // Prepared display path: one invalidated grown cache, held until aggregate transfer.
    std::optional<ColorEditGrid> grownGrid; // Planned extent on the original grid, before worker preparation.
    LayerTransform grownTransform;
    double grownMargin = 0;
    int grownOffsetX = 0; // where the original grid sits in the grown one
    int grownOffsetY = 0;
    Raster previewSource;    // the source no larger than the kind's preview limit
    double previewScale = 1; // pixels in previewSource per source pixel
    Raster previewRaster;
    Raster previewCoverage;
    std::shared_ptr<const AdjustmentLookup> previewLookup;
    bool previewLookupOnly = false; // live adjustment metadata, without a destructive layer-grid target
    LayerTransform previewTransform;
    // An automatic filter that found nothing to work from (Content-Aware Fill without opaque surroundings): the
    // panel says so and OK waits.
    bool previewFailed = false;
    // Levels: the histogram of the original (alpha and selection weighted) and the armed eyedropper.
    LevelsHistogram histogram{};
    bool histogramReady = false;
    std::optional<LevelsSample> levelsSample;
    // Hue/Saturation: the armed eyedropper and the targeted adjustment with its drag.
    std::optional<HueSampleMode> hueSample;
    bool targeting = false;
    std::optional<HueTargetDrag> targetDrag;
    [[nodiscard]] const Raster& Source() const noexcept
    {
        return grownImage.HasStorage() ? grownImage : original;
    }
    [[nodiscard]] const LayerTransform& Placement() const noexcept
    {
        return grownGrid.has_value() || grownImage.HasStorage() ? grownTransform : transform;
    }
    [[nodiscard]] int SourceWidth() const noexcept
    {
        return grownGrid ? grownGrid->width : Source().Width();
    }
    [[nodiscard]] int SourceHeight() const noexcept
    {
        return grownGrid ? grownGrid->height : Source().Height();
    }

  private:
    friend class DocumentSession;
    friend std::uint64_t ColorEditHeldMemoryBytes(const ColorEdit&) noexcept;
    // Only an acknowledged preserving-panel Wand may bind a new document selection for future jobs.
    // Original public sampling/coverage/source-token state remains unchanged. Keeping this owner inside
    // the panel also carries it through every existing capture, Cancel/OK, replacement and retirement swap.
    struct WandSelectionBinding final
    {
        std::uint64_t opening = 0;
        std::optional<DocumentSelection> selection;
    };
    std::optional<WandSelectionBinding> _wandSelectionBinding;
};

// What the last brush commit installed: the raster and the grid rectangle it covers (for the host's live preview).
struct BrushCommitInfo
{
    Raster image;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    bool mask = false;
};

struct HierarchyEntry
{
    Guid id;
    std::wstring name;
    int depth = 0;
    bool visible = true;
    bool isGroup = false;
    bool collapsed = false;
    bool hasMask = false;    // the layers panel's Mask badge (RF-907)
    bool maskEnabled = true; // false for a disabled mask
};

// What a mask shows beyond its pixels once placed apart from its layer: 1 (reveal) or 0 (hide), whichever most
// of its edge is, read from the thumbnail (legacy LayerMask.background). A missing thumbnail reveals.
[[nodiscard]] double MaskEdgeBackground(const Raster& thumbnail) noexcept;

// A mask on its layer's pixel grid (`oldWidth` × `oldHeight`; stretched onto it first when its own size differs)
// carried onto a new grid of that layer: the old grid's top-left lands at (`x`, `y`) of the `width` × `height` result
// and only the area beyond the old grid reads `outside`. A sparse mask keeps its tiles and its own fill. Empty when
// the sizes are out of range.
[[nodiscard]] Raster CarriedMask(const Raster& mask, int oldWidth, int oldHeight, int x, int y, int width, int height,
                                 std::uint8_t outside);

// Where a mask's pixels sit on the document: its own placement, else the layer's transform.
[[nodiscard]] inline LayerTransform MaskTransform(const ImageLayer& layer) noexcept
{
    if (layer.mask.has_value() && layer.mask.value().placement.has_value())
    {
        return layer.mask.value().placement.value();
    }
    return layer.transform;
}

[[nodiscard]] std::wstring TrimWide(std::wstring_view text);
[[nodiscard]] bool NameUtf8LengthOk(std::wstring_view name) noexcept;
[[nodiscard]] DocumentStatus ValidateHierarchy(std::span<const ImageLayer> layers);
[[nodiscard]] DocumentStatus ValidateLiveMaskGraph(std::span<const ImageLayer> layers);
[[nodiscard]] DocumentStatus ValidateDocument(const CanvasDocument& document,
                                              int formatVersion = kCurrentFormatVersion);
[[nodiscard]] std::vector<HierarchyEntry> HierarchyEntries(std::span<const ImageLayer> layers, bool topFirst = false,
                                                           const std::unordered_set<Guid>& collapsed = {});
[[nodiscard]] std::vector<Guid> RenderLayerIds(const CanvasDocument& document);
// A layer shown: it and every folder above it are visible.
[[nodiscard]] bool EffectivelyVisible(std::span<const ImageLayer> layers, const Guid& id) noexcept;
[[nodiscard]] inline bool EffectivelyVisible(const CanvasDocument& document, const Guid& id) noexcept
{
    return EffectivelyVisible(std::span<const ImageLayer>(document.layers), id);
}
[[nodiscard]] std::unordered_set<Guid> DescendantIds(std::span<const ImageLayer> layers, const Guid& id);
// `id`'s position in `layers`; nullopt when it is not there.
[[nodiscard]] std::optional<std::size_t> IndexOf(std::span<const ImageLayer> layers, const Guid& id) noexcept;

// Immutable full roots use their worker-prepared key index; sliced/vector ranges keep span-relative order.
[[nodiscard]] inline std::optional<std::size_t> IndexOf(const RecordRoot<ImageLayer>& layers, const Guid& id) noexcept
{
    return layers.FindIndex(id);
}

// Immutable owning closure of one document's storage graph. Prepared on a worker; copies retain one handle.
// Raster charges are read at use time so a sparse cache published later is not represented by a stale total.
// This is storage topology, not the resident-charge/publication synchronization protocol.
struct HistoryRasterRootGroup;
class HistoryStorageTopology final
{
  private:
    struct ConstructionTag final
    {
    };

  public:
    explicit HistoryStorageTopology(ConstructionTag) noexcept {}
    [[nodiscard]] static std::shared_ptr<const HistoryStorageTopology> Prepare(
        const std::optional<CanvasDocument>& document, CancelToken cancellation = {},
        std::shared_ptr<const SharedPublicationMemory> memory = {});
    [[nodiscard]] bool Matches(const std::optional<CanvasDocument>& document) const noexcept;
    // Managed preparation captures direct source roots only, sharing exact scalar-equivalent owners.
    // The reference Prepare/RasterOwners API keeps its complete closure semantics and independent goldens.
    [[nodiscard]] static std::shared_ptr<const HistoryStorageTopology> PrepareIncremental(
        const std::optional<CanvasDocument>& document, std::shared_ptr<const HistoryStorageTopology> previous = {},
        CancelToken cancellation = {}, std::shared_ptr<const SharedPublicationMemory> memory = {});
    [[nodiscard]] std::span<const Raster> RasterRoots() const noexcept;
    [[nodiscard]] const void* RasterRootIdentity() const noexcept;
    [[nodiscard]] bool HasRasterRoot(std::uint64_t serial) const noexcept;
    [[nodiscard]] std::uint64_t SharedRootMetadataBytes() const noexcept;
    [[nodiscard]] const std::vector<Raster>& RasterOwners() const noexcept
    {
        return _rasters;
    }
    [[nodiscard]] const SelectionLoops& SelectionOwner() const noexcept
    {
        return _selection;
    }
    [[nodiscard]] const RecordRoot<ImageLayer>& RecordOwner() const noexcept
    {
        return _layers;
    }
    [[nodiscard]] std::uint64_t MetadataBytes() const noexcept
    {
        return _metadataBytes;
    }
    [[nodiscard]] std::uint64_t RetirementMemoryBytes() const noexcept
    {
        return _retirementBytes;
    }

  private:
    RecordRoot<ImageLayer> _layers;
    SelectionLoops _selection;
    std::vector<Raster> _rasters;
    std::shared_ptr<const HistoryRasterRootGroup> _managedRoots;
    std::uint64_t _metadataBytes = 0;
    std::uint64_t _retirementBytes = 0;
    bool _hasDocument = false;
};

class DocumentHistory
{
  private:
    struct Entry;
    struct Pending;
    struct StorageIndex;
    using EntryHandle = std::shared_ptr<const Entry>;

  public:
    struct Snapshot
    {
        std::optional<CanvasDocument> document;
        std::optional<Guid> activeLayerID;
        Guid revision;
        // Public value snapshots may be edited by reference tests. Consumers validate Matches before using
        // this immutable index; published entry snapshots are const and keep their complete owners alive.
        std::shared_ptr<const HistoryStorageTopology> storageTopology;
        [[nodiscard]] std::optional<RecordOwnershipBounds> OwnershipBounds() const noexcept;
    };

    explicit DocumentHistory(int entryLimit = kHistoryEntryLimit,
                             std::uint64_t retainedByteLimit = kHistoryRetainedByteLimit) noexcept;

    // Owning immutable handles only: capturing the history stacks never copies layer, raster or outline records.
    // Capture shares two immutable stack roots without allocation or handle-vector traversal.
    // Saved-revision publication is independent of this structural epoch and never invalidates an edit view.
    struct StateView final
    {
        StateView() = default;
        StateView(const StateView&) = default;
        StateView& operator=(const StateView&) = default;
        StateView(StateView&& source) noexcept
            : past(std::move(source.past)), future(std::move(source.future)), pending(std::move(source.pending)),
              storageIndex(std::move(source.storageIndex)), publicationMemory(std::move(source.publicationMemory)),
              owner(std::exchange(source.owner, nullptr)), revision(source.revision), epoch(source.epoch),
              depth(source.depth), entryLimit(source.entryLimit), byteLimit(source.byteLimit),
              requirePredecessor(std::exchange(source.requirePredecessor, false)),
              precedingPast(std::move(source.precedingPast)), precedingFuture(std::move(source.precedingFuture)),
              precedingPending(std::move(source.precedingPending)), precedingDepth(source.precedingDepth)
        {
        }
        StateView& operator=(StateView&& source) noexcept
        {
            if (this != &source)
            {
                past = std::move(source.past);
                future = std::move(source.future);
                pending = std::move(source.pending);
                storageIndex = std::move(source.storageIndex);
                publicationMemory = std::move(source.publicationMemory);
                owner = std::exchange(source.owner, nullptr);
                revision = source.revision;
                epoch = source.epoch;
                depth = source.depth;
                entryLimit = source.entryLimit;
                byteLimit = source.byteLimit;
                requirePredecessor = std::exchange(source.requirePredecessor, false);
                precedingPast = std::move(source.precedingPast);
                precedingFuture = std::move(source.precedingFuture);
                precedingPending = std::move(source.precedingPending);
                precedingDepth = source.precedingDepth;
            }
            return *this;
        }
        [[nodiscard]] Guid Revision() const noexcept
        {
            return revision;
        }
        [[nodiscard]] int Depth() const noexcept
        {
            return depth;
        }
        [[nodiscard]] std::size_t PastCount() const noexcept
        {
            return past.size();
        }
        [[nodiscard]] std::size_t FutureCount() const noexcept
        {
            return future.size();
        }
        [[nodiscard]] const std::wstring& UndoName() const noexcept;
        [[nodiscard]] const std::wstring& RedoName() const noexcept;

      private:
        friend class DocumentHistory;
        HistoryEntries<EntryHandle> past;
        HistoryEntries<EntryHandle> future;
        std::shared_ptr<const Pending> pending;
        std::shared_ptr<const StorageIndex> storageIndex;
        // Explicit worker construction scope; newly allocated roots retain this owner independently.
        std::shared_ptr<const SharedPublicationMemory> publicationMemory;
        const DocumentHistory* owner = nullptr; // opaque identity only; workers never dereference the live history
        Guid revision;
        std::uint64_t epoch = 0;
        int depth = 0;
        int entryLimit = 0;
        std::uint64_t byteLimit = 0;
        // A dependent publication owns the exact predecessor roots, preventing another publication at
        // the same epoch/revision from satisfying its authority. Capturing these handles never allocates.
        bool requirePredecessor = false;
        HistoryEntries<EntryHandle> precedingPast;
        HistoryEntries<EntryHandle> precedingFuture;
        std::shared_ptr<const Pending> precedingPending;
        int precedingDepth = 0;
    };
    [[nodiscard]] StateView CaptureState() const noexcept;
    [[nodiscard]] bool IsCurrent(const StateView& view) const noexcept;

    // Display-only work validates structure without owning an unrelated history stack. The owner identity is
    // opaque on workers; its monotonic epoch also guards replacement with the same revision/contents.
    class Authority final
    {
      public:
        Authority() noexcept = default;

      private:
        friend class DocumentHistory;
        const DocumentHistory* _owner = nullptr;
        std::uint64_t _epoch = 0;
        Guid _revision;
    };
    [[nodiscard]] Authority CaptureAuthority() const noexcept;
    [[nodiscard]] bool IsCurrent(const Authority& authority) const noexcept;

    enum class PrepareStage : std::uint8_t
    {
        BeforeSnapshot,
        AfterSnapshot,
        Entry,
        StackGrowth,
        TrimIndex,
        PendingSnapshot,
        PendingRecord,
        RestoreSnapshot,
        ReplacementReset,
        ReplacementSnapshot,
        AdjustmentSnapshot,
        SelectionSnapshot,
        StorageIndexClone,
        StorageIndexMembership,
        StorageIndexBounds,
        StorageIndexResidencyRecord,
        StorageIndexResidencyFullScan,
        StorageIndexHistoryEdge,
        StorageIndexLiveEdge,
    };
    // Borrowed for this preparation only. Deterministic worker tests may cancel or throw bad_alloc at these
    // construction boundaries. Production leaves it empty; callbacks never consult live history or UI.
    struct PrepareObserver final
    {
        void (*checkpoint)(void*, PrepareStage) = nullptr;
        void* context = nullptr;
    };

    class ResidencyPermit;
    class ReplacementPublication;
    struct Publication final
    {
        Publication() = default;
        ~Publication() noexcept;
        Publication(const Publication&) = delete;
        Publication& operator=(const Publication&) = delete;
        Publication(Publication&& source) noexcept;
        Publication& operator=(Publication&& source) noexcept;
        [[nodiscard]] const std::optional<Snapshot>& RestoreSnapshot() const noexcept
        {
            return restore;
        }
        [[nodiscard]] std::optional<Snapshot> TakeRestoreSnapshot() noexcept
        {
            if (!published)
                return std::nullopt;
            auto result = std::move(restore);
            restore.reset();
            return result;
        }
        [[nodiscard]] bool ChangesRevision() const noexcept
        {
            return state.revision != expectedRevision;
        }

      private:
        friend class DocumentHistory;
        friend class ResidencyPermit;
        StateView state;
        std::optional<Snapshot> restore;
        Guid expectedRevision;
        std::uint64_t expectedEpoch = 0;
        bool published = false;
        bool sampledResidency = false;
        bool requiresResidencyPermit = false;
        ResidencyPermit* residencyPermit = nullptr; // borrowed only within the acquiring owner's completion
    };
    enum class ResidencyValidation : std::uint8_t
    {
        Ready,
        Refresh,
        Busy,
        Invalid
    };
    // Opt-in owner completion guard. TryAcquire never waits or allocates; Busy/Refresh retain the edit intent
    // for a worker resume. It is not document authority. Reset before any wait, transfer or callback; successful
    // Publish releases it immediately after the history swap. Moving a publication releases its borrowed permit
    // and requires a fresh permit at the new destination. Default/reference publication remains unchanged.
    class ResidencyPermit final
    {
      public:
        ResidencyPermit() noexcept = default;
        ~ResidencyPermit()
        {
            Reset();
        }
        ResidencyPermit(const ResidencyPermit&) = delete;
        ResidencyPermit& operator=(const ResidencyPermit&) = delete;
        [[nodiscard]] ResidencyValidation TryAcquire(Publication& publication) noexcept;
        [[nodiscard]] ResidencyValidation TryAcquire(ReplacementPublication& publication) noexcept;
        void Reset() noexcept;

      private:
        Publication* _publication = nullptr;
        Raster::ResidencyLease _lease;
    };
    // Worker-only history refresh over the original prepared document; no pixel operation, new revision,
    // authority or ticket is created. A failed/cancelled refresh preserves the original publication. Host
    // admission must cover the extra candidate-index/container peak before using this staged protocol.
    [[nodiscard]] static bool RefreshPreparedResidency(Publication& publication,
                                                       const std::optional<CanvasDocument>& current,
                                                       CancelToken cancellation = {}, PrepareObserver observer = {});
    // Owner-only constant-time hint. It never samples raster bytes or changes history. Empty history needs no
    // maintenance. Reference-written history with no index must be rebuilt on the worker before a budget claim.
    [[nodiscard]] bool NeedsResidencyMaintenance() const noexcept;
    // Worker-only maintenance of the captured current history, with no user edit or new/saved revision.
    // Normal publication authority/permit and full reservation still apply. The host must serialize it with
    // accepted document writers and retire replaced containers off owner; this API alone does not wire notices.
    [[nodiscard]] static std::optional<Publication> PrepareResidencyMaintenance(
        StateView view, const std::optional<CanvasDocument>& current, CancelToken cancellation = {},
        PrepareObserver observer = {});
    // Detached preparation owns all snapshot, entry, container and trim allocations. nullopt means cancellation
    // or ineligible/default state. Allocation failures propagate to the standard worker runner. The returned
    // publication must survive apply/drop and retire on the worker lane: Publish swaps retired live containers
    // into it rather than destroying history on the owner. No worker ever consults live mutable history.
    [[nodiscard]] static std::optional<Publication> PrepareEdit(
        StateView view, const std::optional<CanvasDocument>& before, const std::optional<Guid>& beforeActive,
        const std::optional<CanvasDocument>& after, const std::optional<Guid>& afterActive, std::wstring name,
        CancelToken cancellation = {}, PrepareObserver observer = {},
        std::shared_ptr<const SharedPublicationMemory> memory = {});
    [[nodiscard]] static std::optional<Publication> PrepareRestore(StateView view, bool redo,
                                                                   CancelToken cancellation = {},
                                                                   PrepareObserver observer = {});
    [[nodiscard]] static std::optional<Publication> PrepareBegin(
        StateView view, const std::optional<CanvasDocument>& before, const std::optional<Guid>& active,
        std::wstring name, CancelToken cancellation = {}, PrepareObserver observer = {},
        std::shared_ptr<const SharedPublicationMemory> memory = {});
    [[nodiscard]] static std::optional<Publication> PrepareEnd(
        StateView view, const std::optional<CanvasDocument>& after, const std::optional<Guid>& active,
        CancelToken cancellation = {}, PrepareObserver observer = {},
        std::shared_ptr<const SharedPublicationMemory> memory = {});
    [[nodiscard]] bool CanPublish(const Publication& publication) const noexcept;
    // Prepare a dependent closure before the first publication becomes visible. The resulting view belongs to
    // the exact next structural epoch and can publish only after this predecessor successfully publishes.
    [[nodiscard]] static std::optional<StateView> FollowingPreparedState(const Publication& publication) noexcept;
    [[nodiscard]] bool Publish(Publication& publication) noexcept;

    // A new document has its own saved baseline. Additional images make one entry relative to that baseline.
    // The result still validates the old owner's exact epoch/revision and retains retired containers after apply.
    class ReplacementPublication final
    {
      public:
        ReplacementPublication(const ReplacementPublication&) = delete;
        ReplacementPublication& operator=(const ReplacementPublication&) = delete;
        ReplacementPublication(ReplacementPublication&&) noexcept = default;
        ReplacementPublication& operator=(ReplacementPublication&&) noexcept = default;
        [[nodiscard]] Guid BaselineRevision() const noexcept
        {
            return savedRevision;
        }

      private:
        friend class DocumentHistory;
        friend class ResidencyPermit;
        ReplacementPublication(Publication prepared, Guid baseline) noexcept
            : history(std::move(prepared)), savedRevision(baseline)
        {
        }
        Publication history;
        Guid savedRevision;
    };
    [[nodiscard]] static bool RefreshPreparedResidency(ReplacementPublication& publication,
                                                       const std::optional<CanvasDocument>& current,
                                                       CancelToken cancellation = {}, PrepareObserver observer = {});
    [[nodiscard]] static std::optional<ReplacementPublication> PrepareReplacement(
        StateView view, const std::optional<CanvasDocument>& baseline, const std::optional<Guid>& baselineActive,
        const std::optional<CanvasDocument>& after, const std::optional<Guid>& afterActive, std::wstring name,
        CancelToken cancellation = {}, PrepareObserver observer = {});
    [[nodiscard]] bool CanPublishReplacement(const ReplacementPublication& publication) const noexcept;
    // New/Close save the final replacement state, including New's one entry from an absent baseline.
    [[nodiscard]] static std::optional<ReplacementPublication> PrepareSavedReplacement(
        StateView view, const std::optional<CanvasDocument>& after, const std::optional<Guid>& afterActive,
        std::wstring name, CancelToken cancellation = {}, PrepareObserver observer = {});
    [[nodiscard]] bool PublishReplacement(ReplacementPublication& publication) noexcept;

    void Begin(std::wstring name, const std::optional<CanvasDocument>& document, const std::optional<Guid>& selection);
    void End(const std::optional<CanvasDocument>& document, const std::optional<Guid>& selection);
    [[nodiscard]] std::optional<Snapshot> Undo();
    [[nodiscard]] std::optional<Snapshot> Redo();
    void MarkSaved() noexcept;
    void MarkSaved(const Guid& revision) noexcept;
    void Reset();

    [[nodiscard]] Guid CurrentRevision() const noexcept
    {
        return _revision;
    }

    [[nodiscard]] bool CanUndo() const noexcept
    {
        return _depth == 0 && !_past.empty();
    }
    [[nodiscard]] bool CanRedo() const noexcept
    {
        return _depth == 0 && !_future.empty();
    }
    [[nodiscard]] const std::wstring& UndoName() const noexcept;
    [[nodiscard]] const std::wstring& RedoName() const noexcept;
    [[nodiscard]] bool IsModified() const noexcept
    {
        return _revision != _savedRevision;
    }
    [[nodiscard]] int UndoCount() const noexcept
    {
        return static_cast<int>(_past.size());
    }
    [[nodiscard]] std::size_t RetainedBytes(const std::optional<CanvasDocument>& current) const;
    // Conservative allocation declaration for Begin/End metadata copies, entry growth and trim bookkeeping.
    // Shared pixel/selection inputs are already resident. Zero refuses overflow or excessive graph depth.
    [[nodiscard]] std::uint64_t EditMemoryBytes(const std::optional<CanvasDocument>& current) const noexcept;
    [[nodiscard]] std::uint64_t RestoreMemoryBytes(const std::optional<CanvasDocument>& current,
                                                   bool redo) const noexcept;
    // Conservative held-graph capacity for retirement, including current, pending, past/future and an optional
    // additional raster owner. Prepared stacks use a worker-sealed unique owning index; reference stacks and
    // unmatched/pending/additional sources retain occurrence bounds. No set/allocation/materialization on read.
    // Includes a possible sparse materialization cache. Zero refuses overflow or a graph deeper than 128.
    // Reads construction-cached immutable stack/document bounds only. Exact retained-storage indexes remain H3.
    [[nodiscard]] std::uint64_t RetirementMemoryBytes(const std::optional<CanvasDocument>& current,
                                                      const Raster& additional = {}) const noexcept;
    [[nodiscard]] int EntryLimit() const noexcept
    {
        return _entryLimit;
    }
    [[nodiscard]] std::uint64_t RetainedByteLimit() const noexcept
    {
        return _retainedByteLimit;
    }

  private:
    struct Entry
    {
        std::wstring name;
        std::shared_ptr<const Snapshot> before;
        std::shared_ptr<const Snapshot> after;
        [[nodiscard]] std::optional<RecordOwnershipBounds> OwnershipBounds() const noexcept;
    };

    void Trim(const std::optional<CanvasDocument>& current);
    [[nodiscard]] static std::shared_ptr<const HistoryStorageTopology> CurrentStorageTopology(
        const StateView& state) noexcept;
    [[nodiscard]] static std::shared_ptr<const HistoryStorageTopology> PrepareTopology(
        const std::optional<CanvasDocument>& document, const StateView& state, CancelToken cancellation);
    [[nodiscard]] static std::shared_ptr<const Snapshot> SealSnapshot(Snapshot snapshot, const StateView& state,
                                                                      CancelToken cancellation = {});
    [[nodiscard]] static bool TrimPrepared(StateView& state, const std::optional<CanvasDocument>& current,
                                           CancelToken cancellation, PrepareObserver observer);

    struct Pending
    {
        std::wstring name;
        std::shared_ptr<const Snapshot> before;
        [[nodiscard]] std::optional<RecordOwnershipBounds> OwnershipBounds() const noexcept;
    };
    HistoryEntries<EntryHandle> _past;
    HistoryEntries<EntryHandle> _future;
    Guid _revision = Guid::Generate();
    Guid _savedRevision;
    std::shared_ptr<const Pending> _pending;
    std::shared_ptr<const StorageIndex> _storageIndex;
    std::uint64_t _stateEpoch = 1;
    int _depth = 0;
    int _entryLimit = kHistoryEntryLimit;
    std::uint64_t _retainedByteLimit = kHistoryRetainedByteLimit;
};

class DocumentSession
{
  public:
    explicit DocumentSession(std::uint64_t retainedByteLimit = kHistoryRetainedByteLimit)
        : history(kHistoryEntryLimit, retainedByteLimit)
    {
    }

    DocumentHistory history;

    void CreateDocument(int width, int height, bool emptyLayer = false);
    void AddBlankLayer();
    void AddGroup();
    void RenameLayer(const Guid& id, std::wstring_view name);
    void ToggleVisibility(const Guid& id);
    // `above` inserts just after that sibling (visually above it in the top-first list). `before` inserts just
    // in front of that sibling instead (visually below it). `atBottom` is the root's bottom when `above` is empty.
    [[nodiscard]] bool PlaceLayer(const Guid& id, const std::optional<Guid>& parent,
                                  const std::optional<Guid>& above = std::nullopt, bool atBottom = false,
                                  bool before = false);
    void MoveActiveLayerOutOfGroup();
    // C16 (Specs/Document/Document_Model.md "Session edits"): the Layer menu's ordering, grouping and clipping
    // toggle, and the empty state after Close Project.
    [[nodiscard]] bool CanMoveActiveLayer(int offset) const;
    void MoveActiveLayer(int offset);
    void GroupSelectedLayers();
    [[nodiscard]] bool CanToggleClippingMask(const Guid& id) const;
    void ToggleClippingMask(const Guid& id);
    void ClearDocument();
    // A loaded or new document takes the session over (File > Open, a new project from images): everything
    // ClearDocument drops, then `document` with `active` as the only selected layer (its layer, not its mask,
    // targeted) and an unmodified history.
    void ReplaceDocument(CanvasDocument document, const std::optional<Guid>& active);
    // A resized document (Canvas Size, Image Size, Crop: the same id, every layer still there) installed as one
    // entry named `name`; the active layer stays.
    void ApplyDocumentSize(CanvasDocument document, std::wstring name);
    [[nodiscard]] bool ApplyDocumentSizeIfCurrent(CanvasDocument document, std::wstring name, const JobToken& token);
    [[nodiscard]] bool ApplyPreparedDocumentSizeIfCurrent(PreparedDocumentSize& prepared) noexcept;
    [[nodiscard]] std::uint64_t DocumentSizeRetirementMemoryBytes() const noexcept;
    // Installs a validated import assembled from this document as one entry. A different document or invalid
    // graph/budget is refused before any session state changes. The synchronous host prepares this copy today.
    [[nodiscard]] bool ApplyImportedDocument(CanvasDocument document, const std::optional<Guid>& active,
                                             std::wstring name);
    [[nodiscard]] static std::optional<PreparedImageImport> PrepareImageImportPublication(
        ImageImportAuthority authority, std::optional<CanvasDocument> source, std::optional<Guid> sourceActive,
        DocumentHistory::StateView history, ImageImportBatchResult imported, CancelToken cancellation = {},
        DocumentHistory::PrepareObserver observer = {});
    [[nodiscard]] bool ApplyPreparedImageImportIfCurrent(PreparedImageImport& prepared) noexcept;
    [[nodiscard]] static std::optional<PreparedImageImport> PrepareDocumentReplacementPublication(
        ImageImportAuthority authority, std::optional<CanvasDocument> source, DocumentHistory::StateView history,
        CanvasDocument replacement, std::optional<Guid> active, CancelToken cancellation = {},
        DocumentHistory::PrepareObserver observer = {});
    [[nodiscard]] std::uint64_t ImageImportRetirementMemoryBytes() const noexcept;
    [[nodiscard]] static std::optional<PreparedImageImport> PrepareDocumentLifecyclePublication(
        ImageImportAuthority authority, std::optional<CanvasDocument> source, DocumentHistory::StateView history,
        std::optional<CanvasDocument> replacement, std::optional<Guid> active, std::wstring name,
        CancelToken cancellation = {}, DocumentHistory::PrepareObserver observer = {});
    // Lifecycle preparation depends only on sealed lifetime authority, history roots and the new state.
    // Publication itself transfers the actual old document into retirement without an owner-side record copy.
    [[nodiscard]] static std::optional<PreparedImageImport> PrepareDocumentLifecyclePublication(
        ImageImportAuthority authority, DocumentHistory::StateView history, std::optional<CanvasDocument> replacement,
        std::optional<Guid> active, std::wstring name, CancelToken cancellation = {},
        DocumentHistory::PrepareObserver observer = {});
    [[nodiscard]] ImageImportAuthority CaptureImageImportAuthority() const noexcept;
    [[nodiscard]] bool CanApplyImageImport(const ImageImportAuthority& authority) const noexcept;
    // Crop tool (C16, legacy Crop.swift): the frame lives in the session until Apply Crop resizes the canvas to
    // it. The host sets the whole canvas when the tool is chosen and cancels when it goes.
    [[nodiscard]] const std::optional<CropRect>& CropFrame() const noexcept
    {
        return _cropRect;
    }
    void SetCropFrame(std::optional<CropRect> rect) noexcept;
    // The frame shown while the Crop tool is active: the pending one, else the whole canvas.
    [[nodiscard]] std::optional<CropRect> VisibleCropRect() const noexcept;
    int cropRatioChoice = 0; // 0 Free, 1 Original (the canvas ratio), 2 1:1, 3 4:3, 4 16:9
    [[nodiscard]] std::optional<double> CropRatio() const noexcept;
    void CancelCrop() noexcept;
    // The visible frame re-shaped to the chosen ratio, keeping its width and center, when the result is valid.
    void ChangeCropRatio() noexcept;
    // The canvas resized to the frame (the content moved by the frame's origin) as one `Crop` entry; false when
    // there is no valid frame. The frame is dropped either way.
    [[nodiscard]] bool CommitCrop();
    // What crop edges snap to: the canvas edges and every visible pixel layer's upright bounds, in whole pixels.
    [[nodiscard]] SnapTargets CropSnapTargets() const;
    // Merge (C16, legacy LayerMerge.swift): what Ctrl+E merges, in stacking order, and where the result goes. One
    // layer merges with the layer beneath it in the same folder; a folder merges its contents and goes; several
    // selected layers merge together with anything their folders hold.
    struct MergePlan
    {
        std::vector<Guid> ids;
        std::unordered_set<Guid> removed;
        std::wstring name;
        std::optional<Guid> parent;
        Guid anchor;
        std::wstring action; // Merge Down, Merge Group or Merge Layers
    };
    // Worker-safe pixel preparation. The layer and plan snapshot are immutable after construction; the raster
    // handles they own refer to immutable storage. Canvas dimensions let the UI thread reject stale results.
    class PreparedMerge
    {
      public:
        [[nodiscard]] const ImageLayer& Layer() const noexcept
        {
            return _layer;
        }
        [[nodiscard]] int CanvasWidth() const noexcept
        {
            return _canvasWidth;
        }
        [[nodiscard]] int CanvasHeight() const noexcept
        {
            return _canvasHeight;
        }

      private:
        PreparedMerge(ImageLayer mergedLayer, int width, int height, MergePlan sourcePlan)
            : _layer(std::move(mergedLayer)), _canvasWidth(width), _canvasHeight(height), _plan(std::move(sourcePlan))
        {
        }
        ImageLayer _layer;
        int _canvasWidth;
        int _canvasHeight;
        MergePlan _plan;
        friend class DocumentSession;
    };
    [[nodiscard]] std::optional<MergePlan> PlanMerge() const;
    [[nodiscard]] const std::unordered_set<Guid>& SelectedLayerIDs() const noexcept
    {
        return _selected;
    }
    [[nodiscard]] static std::optional<MergePlan> PlanMergeForWork(const std::optional<CanvasDocument>& source,
                                                                   const std::optional<Guid>& active,
                                                                   const std::unordered_set<Guid>& selected,
                                                                   CancelToken cancellation = {});
    // PrepareMerge is worker-safe and owns all pixel trimming and thumbnail work. ApplyPreparedMerge is the
    // session-owned validation/history step; MergeLayers remains the synchronous compatibility wrapper.
    [[nodiscard]] static std::optional<PreparedMerge> PrepareMerge(const MergePlan& plan, const Raster& composite);
    [[nodiscard]] static std::optional<PreparedMerge> PrepareMerge(
        const MergePlan& plan, const Raster& composite, const class CancelToken& cancellation,
        const std::shared_ptr<const void>& outputLifetime = {});
    [[nodiscard]] bool ApplyPreparedMerge(const MergePlan& plan, const PreparedMerge& prepared);
    enum class MergePublicationStatus : std::uint8_t
    {
        Invalid,
        Ready,
        TooLarge,
        Cancelled,
    };
    // Worker-bound pixels, survivor records, selection set and complete history. Owner publication swaps all
    // replaced owners into this candidate; no caller can substitute pixels or history independently.
    class PreparedMergePublication final
    {
      public:
        PreparedMergePublication(const PreparedMergePublication&) = delete;
        PreparedMergePublication& operator=(const PreparedMergePublication&) = delete;
        PreparedMergePublication(PreparedMergePublication&&) noexcept = default;
        PreparedMergePublication& operator=(PreparedMergePublication&&) noexcept = default;

      private:
        friend class DocumentSession;
        PreparedMergePublication(JobToken token, std::optional<Guid> beforeActive, Guid merged, CanvasDocument document,
                                 DocumentHistory::Publication history, std::unordered_set<Guid> selected)
            : _token(std::move(token)), _beforeActive(std::move(beforeActive)), _active(merged),
              _document(std::move(document)), _history(std::move(history)), _selected(std::move(selected))
        {
        }
        JobToken _token;
        std::optional<Guid> _beforeActive;
        std::optional<Guid> _active;
        std::optional<CanvasDocument> _document;
        DocumentHistory::Publication _history;
        std::unordered_set<Guid> _selected;
        std::optional<CanvasDocument> _retiredDisplay;
    };
    [[nodiscard]] static std::optional<PreparedMergePublication> PrepareMergePublication(
        const std::optional<CanvasDocument>& source, const std::optional<Guid>& active, JobToken token,
        DocumentHistory::StateView history, const MergePlan& plan, const Raster& composite,
        MergePublicationStatus& status, CancelToken cancellation = {}, DocumentHistory::PrepareObserver observer = {},
        const std::shared_ptr<const void>& outputLifetime = {});
    [[nodiscard]] bool ApplyPreparedMergeIfCurrent(PreparedMergePublication& prepared) noexcept;
    [[nodiscard]] std::uint64_t MergeRetirementMemoryBytes() const noexcept;
    // `composite`: the plan's layers flattened as the canvas shows them (document-sized BGRA from the composition).
    // The trimmed result replaces them in their place, layers clipped to any of them clip to it, as one entry named
    // by the plan; false when the plan does not fit the document any more.
    [[nodiscard]] bool MergeLayers(const MergePlan& plan, const Raster& composite);
    void DeleteLayer(const Guid& id);
    void SetOpacity(double opacity);
    // The host's typed opacity preview/restore, outside history; callers bracket the final value in an entry.
    // Reference construction may allocate. The production host must prepare the replacement on its admitted
    // worker before this root integration can be activated; owner preview must not call this constructor.
    [[nodiscard]] bool PreviewLayerOpacity(const Guid& id, double opacity);
    void SetBlendMode(LayerBlendMode mode);
    [[nodiscard]] bool LinkMask(const Guid& source, const Guid& target);
    [[nodiscard]] bool CanLinkMask(const Guid& source, const Guid& target) const;
    void BeginEdit(std::wstring name);
    void EndEdit();
    void Undo();
    void Redo();
    [[nodiscard]] HistoryRestoreAuthority CaptureHistoryRestoreAuthority() const noexcept;
    [[nodiscard]] bool CanApplyHistoryRestore(const HistoryRestoreAuthority& authority) const noexcept;
    [[nodiscard]] bool ApplyPreparedHistoryRestoreIfCurrent(PreparedHistoryRestore& prepared) noexcept;
    [[nodiscard]] std::uint64_t HistoryRestoreRetirementMemoryBytes() const noexcept;

    [[nodiscard]] const std::optional<CanvasDocument>& Document() const noexcept
    {
        return _document;
    }
    [[nodiscard]] std::uint64_t Generation() const noexcept
    {
        return _generation;
    }
    [[nodiscard]] std::uint64_t DocumentSerial() const noexcept
    {
        return _documentSerial;
    }
    [[nodiscard]] std::optional<DocumentReadAuthority> CaptureDocumentReadAuthority() const noexcept
    {
        if (!_document)
            return std::nullopt;
        return DocumentReadAuthority{_tokenIdentity.value, _documentSerial,  _generation,
                                     _restoreSerial,       _document->width, _document->height};
    }
    [[nodiscard]] bool IsDocumentReadCurrent(const DocumentReadAuthority& authority) const noexcept
    {
        return _document && authority.sessionIdentity == _tokenIdentity.value &&
               authority.documentSerial == _documentSerial && authority.generation == _generation &&
               authority.restoreSerial == _restoreSerial && authority.canvasWidth == _document->width &&
               authority.canvasHeight == _document->height;
    }
    [[nodiscard]] std::optional<JobToken> CaptureJobToken(const std::optional<Guid>& target = std::nullopt,
                                                          std::uint64_t sequence = 0) const;
    // UI-thread apply check. The caller supplies its current sequence and separately checks cancellation and
    // whether a preview's edit remains open. A token is valid only in the session that captured it.
    [[nodiscard]] bool CanApplyJobResult(const JobToken& token, std::uint64_t latestSequence) const noexcept;
    // Fixture construction only (DocumentTests and the application harness). Never retain the callback's
    // reference. Production mutations use the validated edit APIs. Invalidate first even if the fixture throws.
    template <typename Edit> void EditForTest(Edit&& edit)
    {
        if (_document.has_value())
        {
            NoteDocumentMutation();
            std::invoke(std::forward<Edit>(edit), _document.value());
        }
    }
    [[nodiscard]] const std::optional<Guid>& ActiveLayerID() const noexcept
    {
        return _activeLayerID;
    }
    void SelectLayer(const std::optional<Guid>& id);
    [[nodiscard]] const ImageLayer* ActiveLayer() const noexcept;
    [[nodiscard]] const std::unordered_set<Guid>& CollapsedGroupIDs() const noexcept
    {
        return _collapsed;
    }
    // Explicit collapse writes advance separate display authority, including collapse/expand A-B-A. This
    // state stays outside document history; callers never retain a mutable set across worker capture.
    void SetGroupCollapsed(const Guid& id, bool collapsed);
    void ClearCollapsedGroups() noexcept;
    [[nodiscard]] std::uint64_t CollapseRevision() const noexcept
    {
        return _collapseRevision.Current();
    }
    [[nodiscard]] std::vector<HierarchyEntry> LayerRows() const;
    [[nodiscard]] bool CanPlaceLayer(const Guid& id, const std::optional<Guid>& parent) const;

    // Transform editing (C8, Specs/Document/Document_Tools.md). A pending edit holds a draft; the document changes
    // only on commit, as one history entry. Blank layers (no pixels), hidden layers and folders alone cannot
    // transform; a selected folder transforms its visible pixel layers as a group.
    [[nodiscard]] bool CanTransform() const noexcept;
    [[nodiscard]] bool TransformsAsGroup() const noexcept;
    [[nodiscard]] std::vector<Guid> GroupTransformMemberIDs() const;
    [[nodiscard]] std::optional<LayerTransform> GroupTransformBox() const;
    [[nodiscard]] const std::optional<TransformEdit>& PendingTransformEdit() const noexcept
    {
        return _transformEdit;
    }
    void BeginTransform(bool persistent = true);
    [[nodiscard]] bool BeginDeferredTransform(bool persistent = true);
    [[nodiscard]] bool DeferTransform() noexcept;
    void PreviewTransform(const LayerTransform& value);
    void BeginDistort();
    void PreviewCorners(const std::array<Vec2, 4>& corners);
    void BeginDuplicateTransform();
    void CommitTransform();
    [[nodiscard]] std::uint64_t DuplicateOpeningPreparationMemoryBytes() const;
    [[nodiscard]] std::optional<DuplicateOpeningInput> PrepareDuplicateOpeningInput() const;
    [[nodiscard]] static DuplicateOpeningResult PrepareDuplicateOpeningPublication(
        DuplicateOpeningInput input, Executor& executor, CancelToken cancellation,
        DocumentHistory::PrepareObserver historyObserver, DuplicateOpeningObserver observer);
    [[nodiscard]] bool ApplyPreparedDuplicateOpeningIfCurrent(PreparedDuplicateOpening& prepared) noexcept;
    [[nodiscard]] bool IsDuplicateRestoreCurrent(const PreparedDuplicateRestore& prepared) const noexcept;
    [[nodiscard]] bool ApplyPreparedDuplicateRestoreIfCurrent(PreparedDuplicateRestore& prepared) noexcept;
    [[nodiscard]] std::uint64_t FloatingOpeningPreparationMemoryBytes() const noexcept;
    [[nodiscard]] std::optional<FloatingOpeningInput> PrepareFloatingOpeningInput() const;
    [[nodiscard]] static FloatingOpeningResult PrepareFloatingOpeningPublication(
        FloatingOpeningInput input, Executor& executor, CancelToken cancellation,
        DocumentHistory::PrepareObserver observer, FloatingOpeningObserver openingObserver);
    [[nodiscard]] bool ApplyPreparedFloatingOpeningIfCurrent(PreparedFloatingOpening& prepared) noexcept;
    [[nodiscard]] bool ApplyPreparedFloatingRestoreIfCurrent(PreparedFloatingRestore& prepared) noexcept;
    [[nodiscard]] bool IsFloatingRestoreCurrent(const PreparedFloatingRestore& prepared) const noexcept;
    [[nodiscard]] std::uint64_t FloatingMergePreparationMemoryBytes(
        const PreparedFloatingRestore& restore) const noexcept;
    [[nodiscard]] std::optional<FloatingMergeInput> PrepareFloatingMergeInput(
        std::shared_ptr<PreparedFloatingRestore> restore) const;
    [[nodiscard]] static FloatingMergeResult PrepareFloatingMergePublication(FloatingMergeInput input,
                                                                             Executor& executor,
                                                                             CancelToken cancellation,
                                                                             DocumentHistory::PrepareObserver observer,
                                                                             FloatingMergeObserver mergeObserver);
    [[nodiscard]] bool ApplyPreparedFloatingMergeIfCurrent(PreparedFloatingMerge& prepared) noexcept;
    [[nodiscard]] std::uint64_t TransformCommitPreparationMemoryBytes() const noexcept;
    [[nodiscard]] std::optional<TransformCommitInput> PrepareTransformCommitInput() const;
    [[nodiscard]] static TransformCommitResult PrepareTransformCommitPublication(
        TransformCommitInput input, Executor& executor, CancelToken cancellation = {},
        DocumentHistory::PrepareObserver observer = {});
    [[nodiscard]] bool ApplyPreparedTransformCommitIfCurrent(PreparedTransformCommit& prepared) noexcept;
    [[nodiscard]] std::uint64_t TransformCancellationPreparationMemoryBytes() const noexcept;
    [[nodiscard]] std::optional<TransformCancellationInput> PrepareTransformCancellationInput() const;
    [[nodiscard]] static std::optional<PreparedTransformCancellation> PrepareTransformCancellationPublication(
        TransformCancellationInput input, CancelToken cancellation = {},
        DocumentHistory::PrepareObserver observer = {});
    [[nodiscard]] bool ApplyPreparedTransformCancellationIfCurrent(PreparedTransformCancellation& prepared) noexcept;
    [[nodiscard]] std::uint64_t TransformDraftRevision() const noexcept
    {
        return _transformEdit ? _editRevision : 0;
    }
    [[nodiscard]] std::uint64_t TransformPreviewPreparationMemoryBytes() const noexcept;
    [[nodiscard]] std::optional<TransformPreviewInput> PrepareTransformPreviewInput() const;
    [[nodiscard]] static TransformPreviewResult PrepareTransformPreviewPublication(TransformPreviewInput input,
                                                                                   Executor& executor,
                                                                                   CancelToken cancellation,
                                                                                   TransformPreviewObserver observer);
    [[nodiscard]] bool ApplyPreparedTransformPreviewIfCurrent(PreparedTransformPreview& prepared) noexcept;
    void CancelTransform();
    // Arrow keys: a pending edit moves its draft; without one the move commits at once.
    void NudgeLayer(double dx, double dy);
    // Where a layer's handles sit: the pending draft (the layer's or its mask's), else the layer.
    [[nodiscard]] LayerTransform EditedTransform(const ImageLayer& layer) const;
    // A layer's transform under the pending edit; nullopt when the edit does not move it.
    [[nodiscard]] std::optional<LayerTransform> PendingTransform(const ImageLayer& layer) const;
    [[nodiscard]] LayerTransform DisplayedTransform(const ImageLayer& layer) const;
    [[nodiscard]] std::optional<LayerTransform> DisplayedMaskPlacement(const ImageLayer& layer) const;
    // Pixels the transform places, what 100 % scale draws 1:1; nullopt for a layer without pixels.
    [[nodiscard]] std::optional<Vec2> TransformPixelSize() const;
    [[nodiscard]] bool TransformTargetsMask() const noexcept;
    // Snap targets: canvas edges and center plus every other visible pixel layer's bounds and center.
    [[nodiscard]] SnapTargets TransformSnapTargets(const std::unordered_set<Guid>& excluding) const;
    // `draft` nudged so the layer lines up with a nearby target within `tolerance` document pixels; the guides it
    // met stay in LastSnapGuides until cleared.
    [[nodiscard]] LayerTransform SnappedMove(const LayerTransform& draft, const std::unordered_set<Guid>& moving,
                                             double tolerance);
    [[nodiscard]] const SnapGuides& LastSnapGuides() const noexcept
    {
        return _snapGuides;
    }
    void ClearSnapGuides() noexcept
    {
        _snapGuides = {};
    }
    void FlipLayers(bool horizontally);
    void FlipCanvas(bool horizontally);
    void DuplicateActiveLayer();
    void SelectLayerTarget(const Guid& id, bool mask);
    [[nodiscard]] bool IsMaskSelected() const noexcept
    {
        return _maskSelected;
    }
    void ToggleMaskLink(const Guid& id);
    // A uniform mask on the active layer (`Add Reveal-All Mask` / `Add Hide-All Mask`), then targeted.
    void AddLayerMask(bool revealing = true);
    // Enables or disables a layer's mask (`Enable Layer Mask` / `Disable Layer Mask`).
    void ToggleLayerMask(const Guid& id);
    // The document as the canvas draws it: the pending transform, mask placement and distortion (at preview size)
    // applied to a cached copy that shares every raster with the real document.
    [[nodiscard]] const CanvasDocument* DisplayDocument() const;
    // Borrow only a completed pending source: no clone, pixel derivation or ownership capture.
    [[nodiscard]] bool DescribePendingDisplaySource(PendingDisplaySource& output) const noexcept;

    // Selection (C9, Specs/Document/Document_Tools.md). Outlines live in the document, so history covers them;
    // drafts, the outline-move origin and the header settings are session state.
    [[nodiscard]] bool CanEditSelection() const noexcept;
    [[nodiscard]] const std::optional<DocumentSelection>& Selection() const noexcept;
    // Shift adds, Alt (with or without Shift) subtracts; otherwise the header choice.
    [[nodiscard]] SelectionMode SelectionModeFor(bool shift, bool option) const noexcept;
    // The mode the cursor advertises: an outline in progress keeps its starting mode.
    [[nodiscard]] SelectionMode LassoCursorMode(bool shift, bool option) const noexcept;
    // What the header highlights: the same rule, using the tracked held keys.
    [[nodiscard]] SelectionMode DisplayedSelectionMode() const noexcept;
    // True when the displayed mode changed.
    bool UpdateHeldSelectionKeys(bool shift, bool option) noexcept;
    [[nodiscard]] const std::optional<LassoDraft>& PendingLasso() const noexcept
    {
        return _lassoDraft;
    }
    // The Marquee starts at a whole-pixel anchor in the current marquee kind; the Lasso in the current lasso kind.
    [[nodiscard]] std::optional<LassoOpeningInput> PrepareLassoOpeningInput(Vec2 point, SelectionMode mode,
                                                                            bool marquee);
    // A queued physical descriptor owns these settings. Later header changes cannot replace its kind/AA.
    [[nodiscard]] std::optional<LassoOpeningInput> PrepareLassoOpeningInput(Vec2 point, SelectionMode mode,
                                                                            LassoOpeningSettings settings);
    [[nodiscard]] static std::optional<PreparedLassoOpening> PrepareLassoOpening(
        LassoOpeningInput input, std::uint32_t blocks, CancelToken cancellation,
        std::shared_ptr<const SharedPublicationMemory> memory, bool retainOwnGesture = false);
    [[nodiscard]] bool IsPreparedLassoOpeningCurrent(const PreparedLassoOpening& prepared) const noexcept;
    [[nodiscard]] bool ApplyPreparedLassoOpeningIfCurrent(PreparedLassoOpening& prepared) noexcept;
    // Opaque own-writer authority: a foreign editor/history or direct writer mutation refuses.
    [[nodiscard]] bool IsPreparedLassoGestureCurrent(const PreparedLassoOpening& prepared) const noexcept;
    [[nodiscard]] bool DragPreparedMarqueeIfCurrent(PreparedLassoOpening& prepared, Vec2 point, bool square,
                                                    bool fromCenter) noexcept;
    [[nodiscard]] bool AbortPreparedLassoGestureIfCurrent(PreparedLassoOpening& prepared) noexcept;
    [[nodiscard]] std::optional<LassoFinishTerminal> ClosePreparedLassoGestureForWork(
        PreparedLassoOpening& prepared, bool terminalAntialiased) noexcept;
    // Display capture follows complete display admission; close follows complete finish admission.
    [[nodiscard]] std::optional<LassoFinishTerminal> CaptureBoundedLassoDisplayForWork() const noexcept;
    [[nodiscard]] std::optional<LassoFinishTerminal> CloseBoundedLassoForWork(
        std::optional<bool> finishAntialiased = std::nullopt) noexcept;
    [[nodiscard]] std::optional<LassoFinishInput> PrepareClosedBoundedLassoFinishInput(LassoFinishTerminal terminal);
    [[nodiscard]] bool ExtendBoundedLasso(Vec2 point) noexcept;
    [[nodiscard]] bool MoveBoundedLassoCursor(std::optional<Vec2> point) noexcept;
    [[nodiscard]] bool DragBoundedMarquee(Vec2 point, bool square, bool fromCenter) noexcept;
    [[nodiscard]] bool RemoveLastBoundedLassoPoint() noexcept;
    [[nodiscard]] bool SetLassoOpeningCountersForTest(std::uint64_t opening, std::uint64_t intent) noexcept;
    void BeginMarquee(Vec2 point, SelectionMode mode);
    void BeginLasso(Vec2 point, SelectionMode mode);
    // Marquee drag snapped to whole pixels: `square` evens the sides, `fromCenter` grows around the anchor.
    void DragMarquee(Vec2 point, bool square, bool fromCenter);
    // Adds an outline point; points closer than a quarter pixel are skipped.
    void ExtendLasso(Vec2 point);
    void MoveLassoCursor(std::optional<Vec2> point) noexcept;
    void RemoveLastLassoPoint();
    void CancelLasso() noexcept;
    // Closes the outline and combines it with the selection; a click enclosing nothing deselects in New mode.
    void FinishLasso();
    void ToggleMarqueeKind() noexcept;
    void ToggleLassoKind() noexcept;
    // `shape` clipped to the canvas, combined by `mode` (subtracting from no selection changes nothing), one entry.
    void ApplySelection(const std::vector<SelectionLoop>& shape, SelectionMode mode, std::wstring name);
    // One history entry when the value differs.
    void SetSelection(std::optional<DocumentSelection> value, std::wstring name);
    // True where dragging in New mode would move the selection outline.
    [[nodiscard]] bool CanMoveSelection(Vec2 point) const;
    // Moves the outline only (never pixels); the whole drag is one `Move Selection` entry.
    [[nodiscard]] bool BeginSelectionMove();
    // Explicit worker-mode prototype: document geometry stays immutable until prepared End/Cancel.
    // The host must qualify every consumer/close route before choosing this instead of the reference API.
    [[nodiscard]] bool BeginDeferredSelectionMove();
    // Selected owner path: worker prepares Begin history; publication only validates and swaps roots.
    [[nodiscard]] std::optional<SelectionMoveOpeningInput> PrepareDeferredSelectionMoveOpeningInput() const;
    [[nodiscard]] static std::optional<PreparedSelectionMoveOpening> PrepareDeferredSelectionMoveOpening(
        SelectionMoveOpeningInput input, CancelToken cancellation = {}, DocumentHistory::PrepareObserver observer = {},
        std::shared_ptr<const SharedPublicationMemory> memory = {}, bool prepareAbort = false);
    [[nodiscard]] bool IsPreparedDeferredSelectionMoveOpeningCurrent(
        const PreparedSelectionMoveOpening& prepared) const noexcept;
    [[nodiscard]] bool ApplyPreparedDeferredSelectionMoveOpeningIfCurrent(
        PreparedSelectionMoveOpening& prepared) noexcept;
    // Opaque ownership of the exact published deferred gesture. Only this route renews authority after its
    // own scalar moves. A foreign mutation or history publication refuses both movement and cleanup.
    [[nodiscard]] bool IsPreparedDeferredSelectionMoveGestureCurrent(
        const PreparedSelectionMoveOpening& prepared) const noexcept;
    [[nodiscard]] bool MovePreparedDeferredSelectionMoveIfCurrent(PreparedSelectionMoveOpening& prepared, double dx,
                                                                  double dy) noexcept;
    // Cleanup of an already published opening: uses the worker-prepared no-op End, allocates nothing and
    // restores the reference Cancel generation semantics. It never publishes cancelled worker output.
    [[nodiscard]] bool AbortPreparedDeferredSelectionMoveIfCurrent(PreparedSelectionMoveOpening& prepared) noexcept;

    // Whole-pixel offsets from the drag's start; the outline is not re-clipped, so it can leave and come back.
    void MoveSelection(double dx, double dy);
    void EndSelectionMove();
    // Escape or Undo during the move: the outline goes back where the move began and its entry closes unrecorded.
    void CancelSelectionMove();
    [[nodiscard]] bool IsMovingSelection() const noexcept
    {
        return _selectionMoveOrigin.has_value();
    }
    // Constant-time mode query; no geometry capture or numeric materialization.
    [[nodiscard]] bool IsDeferredSelectionMove() const noexcept
    {
        return HasCurrentDeferredSelectionMoveSource();
    }
    // Session-local event authority. Closing and reopening an equal outline is a distinct move.
    [[nodiscard]] std::optional<std::uint64_t> SelectionMoveOpening() const noexcept
    {
        return _selectionMoveOrigin ? std::optional(_selectionMoveOpening) : std::nullopt;
    }
    // Arrow keys: 1 px, or 10 with Shift; each press is one entry.
    void NudgeSelection(double dx, double dy);
    [[nodiscard]] std::optional<SelectionNudgeInput> PrepareSelectionNudgeInput(int dx, int dy) const;
    [[nodiscard]] static std::optional<PreparedSelectionNudge> PrepareSelectionNudgePublication(
        SelectionNudgeInput input, CancelToken cancellation = {}, DocumentHistory::PrepareObserver observer = {},
        const SelectionNudgeObserver* geometryObserver = nullptr);
    [[nodiscard]] bool IsPreparedSelectionNudgeCurrent(const PreparedSelectionNudge& prepared) const noexcept;
    [[nodiscard]] bool ApplyPreparedSelectionNudgeIfCurrent(PreparedSelectionNudge& prepared) noexcept;
    [[nodiscard]] std::uint64_t SelectionNudgeMemoryBytes() const noexcept;
    // Expand / Contract need a non-empty selection and no outline in progress.
    [[nodiscard]] bool CanModifySelection() const noexcept;
    void ExpandSelection(int amount);
    void ContractSelection(int amount);
    // Admitted owner capture; these sealed preparation APIs leave the direct selection reference routes intact.
    [[nodiscard]] std::optional<SelectionCommandInput> PrepareSelectionCommandInput(SelectionCommandKind kind) const;
    [[nodiscard]] std::optional<SelectionCommandInput> PreparePreservingSelectionCommandInput(
        SelectionCommandKind kind) const;
    [[nodiscard]] static std::optional<PreparedSelectionCommand> PrepareSelectionCommandPublication(
        SelectionCommandInput input, CancelToken cancellation = {}, DocumentHistory::PrepareObserver observer = {},
        std::shared_ptr<const SharedPublicationMemory> memory = {});
    [[nodiscard]] bool IsPreparedSelectionCommandCurrent(const PreparedSelectionCommand& prepared) const noexcept;
    [[nodiscard]] bool ApplyPreparedSelectionCommandIfCurrent(PreparedSelectionCommand& prepared) noexcept;
    [[nodiscard]] std::uint64_t SelectionCommandMemoryBytes(SelectionCommandKind kind) const noexcept;
    // Settled metadata capture follows full graph admission. Worker preparation owns the record draft,
    // validation and complete undo publication; owner application only checks authority and swaps owners.
    [[nodiscard]] std::optional<LayerMetadataInput> PrepareLayerMetadataInput(
        LayerMetadataKind kind, Guid target, std::wstring_view name = {}, double opacity = 1.0,
        LayerBlendMode blendMode = LayerBlendMode::Normal) const;
    // Preserves these gesture openings and any outer history transaction; never permits Undo over a live edit.
    [[nodiscard]] LayerMetadataAuthority CaptureLayerMetadataAuthority(bool preserve = false) const noexcept;
    [[nodiscard]] bool CanApplyLayerMetadataAuthority(const LayerMetadataAuthority& authority) const noexcept;
    [[nodiscard]] std::optional<LayerMetadataInput> PreparePreservedLayerMetadataInput(
        LayerMetadataKind kind, Guid target, std::wstring_view name = {}, double opacity = 1.0,
        LayerBlendMode blendMode = LayerBlendMode::Normal) const;
    // Owner-only after full admission. Preview/restore edits no history; typed closure records opening -> final.
    // The caller retains and validates the typed opening/sequence before capture and owner publication.
    [[nodiscard]] std::optional<LayerMetadataInput> PrepareLayerOpacityPreviewInput(Guid target, double value) const;
    [[nodiscard]] std::optional<LayerMetadataInput> PrepareTypedLayerOpacityInput(Guid target, double opening,
                                                                                  double value) const;
    [[nodiscard]] static std::optional<PreparedLayerMetadata> PrepareLayerMetadataPublication(
        LayerMetadataInput input, CancelToken cancellation, DocumentHistory::PrepareObserver historyObserver,
        LayerMetadataPrepareObserver observer, std::shared_ptr<const SharedPublicationMemory> memory = {});
    [[nodiscard]] bool IsPreparedLayerMetadataCurrent(const PreparedLayerMetadata& prepared) const noexcept;
    [[nodiscard]] bool ApplyPreparedLayerMetadataIfCurrent(PreparedLayerMetadata& prepared) noexcept;
    [[nodiscard]] std::uint64_t LayerMetadataMemoryBytes(LayerMetadataKind kind,
                                                         std::size_t nameUnits = 0) const noexcept;
    [[nodiscard]] std::uint64_t PreservedLayerMetadataMemoryBytes(LayerMetadataKind kind,
                                                                  std::size_t nameUnits = 0) const noexcept;
    [[nodiscard]] std::optional<SelectionOffsetMemoryPlan> PlanSelectionOffsetMemory(int amount) const noexcept;
    [[nodiscard]] std::optional<SelectionOffsetInput> PrepareSelectionOffsetInput(int amount) const;
    [[nodiscard]] static std::optional<PreparedSelectionOffset> PrepareSelectionOffsetPublication(
        SelectionOffsetInput captured, CancelToken cancellation = {}, DocumentHistory::PrepareObserver observer = {},
        std::shared_ptr<const SharedPublicationMemory> memory = {},
        const SelectionOffsetObserver* diagnostic = nullptr);
    [[nodiscard]] bool IsPreparedSelectionOffsetCurrent(const PreparedSelectionOffset& prepared) const noexcept;
    [[nodiscard]] bool ApplyPreparedSelectionOffsetIfCurrent(PreparedSelectionOffset& prepared) noexcept;

    [[nodiscard]] std::optional<InverseSelectionMemoryPlan> PlanInverseSelectionMemory() const noexcept;
    [[nodiscard]] std::optional<InverseSelectionInput> PrepareInverseSelectionInput() const;
    [[nodiscard]] static std::optional<PreparedInverseSelection> PrepareInverseSelectionPublication(
        InverseSelectionInput captured, CancelToken cancellation = {},
        DocumentHistory::PrepareObserver historyObserver = {},
        std::shared_ptr<const SharedPublicationMemory> memory = {}, const InverseSelectionObserver* observer = nullptr);
    [[nodiscard]] bool IsPreparedInverseSelectionCurrent(const PreparedInverseSelection& prepared) const noexcept;
    [[nodiscard]] bool ApplyPreparedInverseSelectionIfCurrent(PreparedInverseSelection& prepared) noexcept;

    [[nodiscard]] std::uint64_t LassoOpeningForWork() const noexcept
    {
        return _lassoOpening;
    }
    [[nodiscard]] std::optional<LassoFinishInput> PrepareLassoFinishInput(LassoFinishTerminal terminal);
    [[nodiscard]] static std::optional<PreparedLassoFinish> PrepareLassoFinishPublication(
        LassoFinishInput input, CancelToken cancellation = {}, DocumentHistory::PrepareObserver historyObserver = {},
        const LassoFinishObserver* observer = nullptr, std::shared_ptr<const SharedPublicationMemory> memory = {});
    [[nodiscard]] bool IsPreparedLassoFinishCurrent(const PreparedLassoFinish& prepared) const noexcept;
    [[nodiscard]] bool ApplyPreparedLassoFinishIfCurrent(PreparedLassoFinish& prepared) noexcept;
    [[nodiscard]] std::optional<WandLayerSelectionInput> PrepareWandLayerSelectionInput(
        Vec2 point, WandSettings settings, bool antialiased, SelectionMode mode = SelectionMode::New) const;
    [[nodiscard]] std::optional<WandLayerSelectionInput> PrepareWandCompositeSelectionInput(
        Vec2 point, WandSettings settings, bool antialiased, SelectionMode mode = SelectionMode::New) const;
    [[nodiscard]] std::optional<PanelWandOpening> CapturePanelWandOpening() const noexcept;
    [[nodiscard]] bool IsPanelWandOpeningCurrent(const PanelWandOpening& opening) const noexcept;
    [[nodiscard]] std::optional<PanelWandAuthority> CapturePanelWandAuthority() const noexcept;
    [[nodiscard]] bool IsPanelWandAuthorityCurrent(const PanelWandAuthority& authority) const noexcept;
    // Explicit live-panel selection route. Every other unresolved edit retains its original refusal.
    // The captured panel opening/sequence survives unchanged; ordinary history restore remains strict.
    [[nodiscard]] std::optional<WandLayerSelectionInput> PreparePanelWandLayerSelectionInput(
        Vec2 point, WandSettings settings, bool antialiased, SelectionMode mode = SelectionMode::New) const;
    [[nodiscard]] std::optional<WandLayerSelectionInput> PreparePanelWandCompositeSelectionInput(
        Vec2 point, WandSettings settings, bool antialiased, SelectionMode mode = SelectionMode::New) const;
    [[nodiscard]] static std::optional<PreparedWandLayerSelection> PrepareWandLayerSelectionPublication(
        WandLayerSelectionInput input, WandFailure* failure = nullptr, CancelToken cancellation = {},
        DocumentHistory::PrepareObserver historyObserver = {}, const WandLayerSelectionObserver* observer = nullptr,
        std::shared_ptr<const SharedPublicationMemory> memory = {});
    [[nodiscard]] static std::optional<PreparedWandLayerSelection> PrepareWandCompositeSelectionPublication(
        WandLayerSelectionInput input, Raster composite, WandFailure* failure = nullptr, CancelToken cancellation = {},
        DocumentHistory::PrepareObserver historyObserver = {}, const WandLayerSelectionObserver* observer = nullptr,
        std::shared_ptr<const SharedPublicationMemory> memory = {});
    [[nodiscard]] bool IsPreparedWandLayerSelectionCurrent(const PreparedWandLayerSelection& prepared) const noexcept;
    [[nodiscard]] bool ApplyPreparedWandLayerSelectionIfCurrent(PreparedWandLayerSelection& prepared) noexcept;
    [[nodiscard]] std::optional<WandLayerSelectionMemoryPlan> PlanWandLayerSelectionMemory(
        Vec2 point, WandSettings settings, SelectionMode mode = SelectionMode::New) const noexcept;
    [[nodiscard]] std::optional<WandLayerSelectionMemoryPlan> PlanWandCompositeSelectionMemory(
        Vec2 point, WandSettings settings, SelectionMode mode = SelectionMode::New) const noexcept;
    [[nodiscard]] std::optional<WandLayerSelectionMemoryPlan> PlanPanelWandLayerSelectionMemory(
        Vec2 point, WandSettings settings, SelectionMode mode = SelectionMode::New) const noexcept;
    [[nodiscard]] std::optional<WandLayerSelectionMemoryPlan> PlanPanelWandCompositeSelectionMemory(
        Vec2 point, WandSettings settings, SelectionMode mode = SelectionMode::New) const noexcept;
    [[nodiscard]] std::optional<SelectionMoveClosureInput> PrepareSelectionMoveClosureInput(
        bool cancel, std::optional<std::uint64_t> expectedOpening = std::nullopt) const;
    [[nodiscard]] static std::optional<PreparedSelectionMoveClosure> PrepareSelectionMoveClosurePublication(
        SelectionMoveClosureInput input, CancelToken cancellation = {}, DocumentHistory::PrepareObserver observer = {},
        const SelectionNudgeObserver* geometryObserver = nullptr, std::shared_ptr<const void> geometryLifetime = {},
        std::shared_ptr<const SharedPublicationMemory> historyMemory = {});
    [[nodiscard]] bool IsPreparedSelectionMoveClosureCurrent(
        const PreparedSelectionMoveClosure& prepared) const noexcept;
    [[nodiscard]] bool ApplyPreparedSelectionMoveClosureIfCurrent(PreparedSelectionMoveClosure& prepared) noexcept;
    [[nodiscard]] std::uint64_t SelectionMoveClosureMemoryBytes() const noexcept;
    [[nodiscard]] std::uint64_t SelectionMoveClosureRetirementMemoryBytes() const noexcept;
    void SelectAll();
    void Deselect();
    void InvertSelection();
    // A mask's black (hidden) areas, or a layer's pixels at least 50 % opaque, become the selection; false when
    // there is nothing to trace.
    // A refused outline is false too, with `failure` saying why (TooDetailed past 8 000 000 pixel edges, Memory), so
    // the host can tell the user instead of silently loading nothing; `failure` is None for "nothing to trace".
    bool LoadMaskSelection(const Guid& id, SelectionMode mode = SelectionMode::New, WandFailure* failure = nullptr);
    bool LoadLayerSelection(const Guid& id, SelectionMode mode = SelectionMode::New, WandFailure* failure = nullptr);
    // Selects pixels similar to the one at `point`, read from `composite` (the visible canvas at document size,
    // required when `wandSettings.sampleAllLayers`) or the active layer's own pixels as placed. One entry.
    WandFailure MagicWand(Vec2 point, SelectionMode mode, const Raster* composite = nullptr);
    // What the wand reads at document size: the composite, or the active layer's pixels without its mask; a
    // folder or blank layer reads as transparent.
    [[nodiscard]] Raster WandSample(const Raster* composite) const;
    LassoKind lassoKind = LassoKind::Freehand;
    LassoKind marqueeKind = LassoKind::Rectangle;
    SelectionMode selectionModeChoice = SelectionMode::New;
    std::optional<SelectionMode> heldSelectionMode;
    bool selectionAntialiased = true;
    WandSettings wandSettings;
    // Expand and Contract take 1 to this many pixels (the amounts are the host's header state).
    static constexpr int kSelectionModifyLimit = 500;

    // Brush and eraser (C10, Specs/Rendering/Rendering_Brush.md). A stroke edits the active layer's pixels or
    // its mask in 256-pixel tiles; the coverage engine (GPU compute in the app, CPU here) computes the tip.
    [[nodiscard]] bool CanPaint() const noexcept;
    // Coverage engine for later strokes; nullptr means the CPU engine.
    void SetBrushEngine(BrushCoverageEngine* engine) noexcept
    {
        _brushEngine = engine;
    }
    // Owner-only after all producers join. Detach borrowed graphics engines without dropping the CPU graph;
    // the final retirement callback then releases the session's large CPU owners away from the owner thread.
    void DetachCoverageForShutdown() noexcept;
    // Final worker only after owner detachment and producer join. Drop all pixel/history/draft/settings owners
    // without allocating or reconstructing a session; empty hash bucket capacity survives until host destruction.
    void RetireCpuForShutdown() noexcept;
    // Starts a stroke at `point` (document pixels): the foreground color, or `erase`; on a mask, black or white
    // per `maskPaintWhite`. False when painting cannot start (or the stroke exceeded the pixel budget).
    bool BeginBrush(Vec2 point, bool erase = false);
    // Capture handles/scalars only after whole opening admission. Worker prepares selection/sample/CPU draft;
    // current owner application attaches the real graphics engine and never reconstructs these pixels.
    [[nodiscard]] std::optional<StrokeOpeningInput> PrepareStrokeOpeningInput(
        StrokeOpeningOptions options) const noexcept;
    [[nodiscard]] static StrokeOpeningResult PrepareStrokeOpeningPublication(StrokeOpeningInput input,
                                                                             CancelToken cancellation,
                                                                             StrokeOpeningObserver observer);
    [[nodiscard]] bool IsPreparedStrokeOpeningCurrent(const PreparedStrokeOpening& prepared) const noexcept;
    [[nodiscard]] bool ApplyPreparedStrokeOpeningIfCurrent(PreparedStrokeOpening& prepared) noexcept;
    // The retouch brushes (C11): Spot Healing and Clone Stamp rework image pixels and never start on a mask; Blur
    // paints a softened copy of the layer or its mask through the tip. Clone Stamp needs a source
    // (`SetCloneSource`) and, with `cloneSettings.sampleAllLayers`, the host's composite of the visible canvas at
    // document size.
    bool BeginBrush(Vec2 point, BrushKind kind, const Raster* composite = nullptr);
    // A captured clone offset selects exactly the supplied committed BGRA canvas, without consulting later
    // clone source/alignment/sample settings. Invalid captured input refuses before any live edit is resolved.
    bool BeginBrush(Vec2 point, BrushKind kind, const Raster* composite, BrushSettings settings, bool maskWhite,
                    std::optional<Vec2> capturedCloneOffset = std::nullopt);
    // Alt-click: where Clone Stamp copies from. A new source starts a new alignment.
    void SetCloneSource(Vec2 point) noexcept;
    // The whole-pixel offset a stroke starting at `point` would copy with: aligned strokes keep the first
    // stroke's; otherwise it runs from the brush to the source. Nullopt without a source.
    [[nodiscard]] std::optional<Vec2> CloneStrokeOffset(Vec2 point) const noexcept;
    // Where the source sits for a brush at `point`, for the canvas's crosshair: the source itself until a stroke
    // fixes the offset.
    [[nodiscard]] std::optional<Vec2> CloneSamplePoint(Vec2 point) const noexcept;
    // What a Clone Stamp stroke copies from, at document size: the active layer's own pixels as placed, or the
    // composite. Empty when there is nothing to copy.
    [[nodiscard]] Raster CloneSample(const Raster* composite) const;
    // What a Blur stroke paints: the active layer (or its mask) as shown, softened by an amount that follows the
    // brush size. Empty when there is nothing to blur.
    [[nodiscard]] Raster BlurSample(bool mask) const;
    // Smudge and Liquify (the Smear tool's warp modes) work on a document-size copy of the active layer's pixels,
    // never on a mask; `FinishWarp` paints the result into the layer along the stroke as one entry (`Liquify`,
    // `Smudge`).
    // Whole-graph admission precedes immutable owner capture. Useful construction and first sample are worker-only.
    [[nodiscard]] std::uint64_t WarpOpeningMemoryBytes(const WarpOpeningOptions& options) const noexcept;
    [[nodiscard]] std::optional<WarpOpeningInput> PrepareWarpOpeningInput(WarpOpeningOptions options) const noexcept;
    [[nodiscard]] static WarpOpeningResult PrepareWarpOpeningPublication(
        WarpOpeningInput input, CancelToken cancellation, WarpOpeningObserver observer,
        std::shared_ptr<const SharedPublicationMemory> memory);
    [[nodiscard]] bool IsPreparedWarpOpeningCurrent(const PreparedWarpOpening& prepared) const noexcept;
    [[nodiscard]] bool ApplyPreparedWarpOpeningIfCurrent(PreparedWarpOpening& prepared) noexcept;
    // Original complete admission precedes capture. Only a joined original worker may mutate this input.
    [[nodiscard]] std::optional<WarpSampleInput> TakeWarpSampleInput(std::uint64_t opening,
                                                                     const WarpSampleMemoryPlan& plan) noexcept;
    [[nodiscard]] bool IsWarpSampleCurrent(const WarpSampleInput& input) const noexcept;
    [[nodiscard]] bool ApplyWarpSampleIfCurrent(WarpSampleInput& input) noexcept;
    [[nodiscard]] bool DiscardWarpSampleIfCurrent(std::uint64_t opening, std::uint64_t sequence) noexcept;
    // Joined original owner captures whole live/update handles for destruction only, never useful publication.
    [[nodiscard]] std::optional<WarpRetirementInput> TakeWarpRetirementInput(
        std::uint64_t opening, std::optional<std::uint64_t> sampleSequence = std::nullopt) noexcept;
    // Capture only after original whole-graph admission. Exclusive worker replay retains this exact Warp kind.
    [[nodiscard]] std::optional<WarpFinishInput> TakeWarpFinishInput(std::uint64_t opening) noexcept;
    [[nodiscard]] std::optional<WarpFinishMemoryPlan> PlanWarpFinishMemory(bool computeCoverage) const noexcept;
    [[nodiscard]] bool DiscardWarpFinishIfCurrent(std::uint64_t opening) noexcept;
    [[nodiscard]] bool HasPendingWarp() const noexcept
    {
        return _warp || _warpUpdatingOpening || _warpFinishingOpening;
    }
    [[nodiscard]] std::optional<std::uint64_t> PendingWarpOpening() const noexcept
    {
        return _warp ? std::optional(_warpOpening)
                     : (_warpUpdatingOpening ? _warpUpdatingOpening : _warpFinishingOpening);
    }
    bool BeginWarp(Vec2 point);
    void ContinueWarp(Vec2 point);
    void FinishWarp();
    void CancelWarp() noexcept;
    [[nodiscard]] const WarpStroke* PendingWarp() const noexcept
    {
        return _warp.get();
    }
    // Owner-only harness observation; the weak handle retains no draft or borrowed graphics owner.
    [[nodiscard]] std::weak_ptr<WarpStroke> WeakPendingWarpForTest() const noexcept
    {
        return _warp;
    }
    [[nodiscard]] WarpStroke* MutableWarp() noexcept
    {
        return _warp.get();
    }
    // Clone Stamp (family 1) and Smear (family 2) keep their own size, hardness and opacity; the other brushes
    // share family 0. Called when the tool changes families; the parked tips start soft at 40 px.
    void SwitchBrushTipFamily(int from, int to) noexcept;

    // Gradient (C11): a pending raster edit on the active layer or its mask, shown through the stroke preview until
    // Apply, Enter, or a tool, layer or target switch commits it (`Gradient`, `Gradient Mask`); Undo, Escape or
    // Cancel drop it. Dragging a new line on the same target replaces the pending one.
    bool BeginGradient(Vec2 point);
    // Metadata-only opening; the host prepares/publishes its private preview and settles before mutations.
    [[nodiscard]] bool BeginDeferredGradient(Vec2 point);
    [[nodiscard]] bool BeginDeferredGradient(Vec2 point, GradientEdit::CapturedInputs inputs);
    [[nodiscard]] bool PublishGradientPreview(std::shared_ptr<BrushStroke>&& raster, const JobToken& token,
                                              std::uint64_t opening, std::uint64_t sequence,
                                              std::shared_ptr<const PreparedRasterPreview> display = {}) noexcept;
    [[nodiscard]] bool ApplyGradientIfCurrent(ImageLayer&& prepared, const JobToken& token, std::uint64_t opening,
                                              std::uint64_t sequence);
    [[nodiscard]] bool ApplyPreparedGradientIfCurrent(PreparedRasterEdit& prepared, std::uint64_t opening,
                                                      std::uint64_t sequence) noexcept;
    void MoveGradient(std::optional<Vec2> start, std::optional<Vec2> end);
    // Re-renders the pending gradient from the current endpoints, settings and palette.
    void RefreshGradient(bool useLiveInputs = true);
    // Straight red, green, blue, alpha of both ends after the style and Reverse. On a mask (the pending gradient's
    // target, else the current one) the ends come from the mask palette: black or white per `maskPaintWhite` for the
    // foreground and the other for the background; the mask takes the first component.
    [[nodiscard]] std::array<std::array<double, 4>, 2> GradientColors() const noexcept;
    // Ends a drag; a click without a line leaves nothing pending.
    void EndGradientDrag();
    void CancelGradient() noexcept;
    void CommitGradient();
    // Switching tools, layers or targets applies the pending gradient, as in Photoshop.
    // Inline callers refuse an unresolved deferred opening; its host must acknowledge worker settlement.
    [[nodiscard]] bool ResolveGradient();
    [[nodiscard]] const GradientEdit* PendingGradient() const noexcept
    {
        return _gradient ? &_gradient.value() : nullptr;
    }
    [[nodiscard]] GradientEdit* MutableGradient() noexcept
    {
        return _gradient ? &_gradient.value() : nullptr;
    }

    // Shape (C11): a drag in whole pixels (Shift squares, Alt grows from the center) becomes a new layer above the
    // active one filled with the foreground color, one entry (`Rectangle`, `Ellipse`); a click makes nothing and
    // the selection is left alone. Shift-U switches the kind.
    static constexpr std::int64_t kMaxShapePixels = 100'000'000;
    void BeginShape(Vec2 point);
    void BeginShape(Vec2 point, ShapeKind kind, double cornerRadius);
    void DragShape(Vec2 point, bool square, bool fromCenter);
    void CancelShape() noexcept;
    void ToggleShapeKind();
    void FinishShape();
    void CommitShape(const ShapeDraft& draft, std::uint32_t foregroundArgb);
    [[nodiscard]] std::uint64_t ShapeEditPreparationMemoryBytes(const ShapeDraft& draft) const noexcept;
    [[nodiscard]] std::optional<ShapeEditInput> PrepareShapeEditInput(const ShapeDraft& draft,
                                                                      std::uint32_t foregroundArgb) const;
    [[nodiscard]] static ShapeEditResult PrepareShapeEditPublication(
        ShapeEditInput input, Executor& executor, CancelToken cancellation, ShapeWorkObserver observer,
        DocumentHistory::PrepareObserver historyObserver = {});
    [[nodiscard]] bool ApplyPreparedShapeEditIfCurrent(PreparedShapeEdit& prepared) noexcept;
    [[nodiscard]] const std::optional<ShapeDraft>& PendingShape() const noexcept
    {
        return _shapeDraft;
    }
    [[nodiscard]] std::optional<std::uint64_t> PendingShapeOpening() const noexcept
    {
        return _shapeDraft ? std::optional(_shapeOpening) : std::nullopt;
    }
    // "Rectangle 1", "Ellipse 2", … skipping names already in the document.
    [[nodiscard]] std::wstring NextShapeName(ShapeKind kind) const;
    // "Layer 1", "Layer 2", … skipping names already in the document.
    [[nodiscard]] std::wstring NextLayerName() const;

    // Pixel edits (C9b, Specs/Document/Document_Tools.md): the selection edits that need raster primitives
    // (`CanPaint`). Fills the selection (the whole canvas without one; an empty selection nothing) with the palette
    // color as one entry (`Fill`, `Fill Mask`); a mask has its own palette, so black hides and white reveals. True when
    // the fill was recorded.
    bool FillSelection(FillSource source);
    // Delete with a selection: image pixels become transparent (`Clear`); on a mask the selection fills with the
    // background color, as in Photoshop.
    // True when the selection was cleared as a history entry. False leaves the document unchanged.
    bool ClearSelectedPixels();
    // Owner-only publication of a complete worker-prepared target. Refusal leaves prepared ownership with the
    // caller, so an aggregate release owner can retire a large sparse result off-thread.
    [[nodiscard]] bool ApplyRasterEditIfCurrent(ImageLayer&& prepared, bool mask, std::wstring name,
                                                const JobToken& token);
    [[nodiscard]] bool ApplyPreparedRasterEditIfCurrent(PreparedRasterEdit& prepared) noexcept;
    [[nodiscard]] std::uint64_t RasterEditRetirementMemoryBytes() const noexcept
    {
        return history.RetirementMemoryBytes(_document, _lastBrushCommit ? _lastBrushCommit->image : Raster{});
    }
    // The Delete key: clears the selection when there is one; otherwise deletes the targeted mask, or the layer.
    void DeleteKeyPressed();
    void DeleteLayerOrMask();
    void DeleteLayerMask();
    // Ctrl+I: inverts the layer's colors (transparency kept) or its mask, inside the selection or across the whole
    // layer without one; a pending transform or gradient applies first. One entry (`Invert`, `Invert Mask`).
    [[nodiscard]] bool CanInvert() const noexcept;
    void InvertPixels();
    // Ctrl-drag / Ctrl-arrow: the selected pixels (or a copy) move with the outline as one entry (`Move Pixels`,
    // `Duplicate Pixels`); false when there is nothing to move (no selection, a mask target, no pixels).
    bool BeginPixelMove(bool duplicate = false);
    // Metadata-only production opening. Its workers own all lifted pixels/coverage and prepared previews.
    [[nodiscard]] bool BeginDeferredPixelMove(bool duplicate = false);
    [[nodiscard]] bool PublishPixelMovePreview(std::shared_ptr<BrushStroke>&& stroke, const JobToken& token,
                                               std::uint64_t opening, std::uint64_t sequence,
                                               std::shared_ptr<const PreparedRasterPreview> display = {}) noexcept;
    [[nodiscard]] bool ApplyPixelMoveIfCurrent(ImageLayer&& prepared, DocumentSelection&& movedSelection,
                                               const JobToken& token, std::uint64_t opening, std::uint64_t sequence);
    [[nodiscard]] bool ApplyPreparedPixelMoveIfCurrent(PreparedRasterEdit& prepared, std::uint64_t opening,
                                                       std::uint64_t sequence) noexcept;
    // Previews the pixels `offset` whole document pixels away; the stored selection stays put until commit.
    void MovePixels(double dx, double dy);
    void FinishPixelMove();
    void CancelPixelMove() noexcept;
    void NudgePixels(double dx, double dy);
    [[nodiscard]] const PixelMove* PendingPixelMove() const noexcept
    {
        return _pixelMove ? &_pixelMove.value() : nullptr;
    }
    [[nodiscard]] PixelMove* MutablePixelMove() noexcept
    {
        return _pixelMove ? &_pixelMove.value() : nullptr;
    }
    // The outline to draw: during a pixel move the original shifted by the drag; while transforming selected
    // pixels it follows the handles (and a distortion).
    [[nodiscard]] std::optional<DocumentSelection> DisplayedSelection() const;
    [[nodiscard]] SelectionDisplayValue DisplayedSelectionValue() const;
    // Add Mask with a selection: a mask on the layer's pixel grid hiding the selection (or, not revealing, showing
    // only it), dropping the selection and targeting the mask (`Add Mask from Selection`); without one,
    // `AddLayerMask`.
    void AddMask(bool revealing = true);
    [[nodiscard]] std::uint64_t MaskEditPreparationMemoryBytes() const noexcept;
    [[nodiscard]] std::optional<MaskEditInput> PrepareMaskEditInput(bool revealing) const;
    [[nodiscard]] static MaskEditResult PrepareMaskEditPublication(
        MaskEditInput input, Executor& executor, CancelToken cancellation, MaskWorkObserver observer,
        DocumentHistory::PrepareObserver historyObserver = {});
    [[nodiscard]] bool ApplyPreparedMaskEditIfCurrent(PreparedMaskEdit& prepared) noexcept;
    // Ctrl+T with a selection transforms the selected pixels on a floating layer; without one, the layer.
    [[nodiscard]] bool CanTransformSelection() const noexcept;
    void TransformCommand();
    void BeginSelectionTransform();
    // Maps the original selection to where the floating pixels are now (nullopt for a plain layer edit).
    [[nodiscard]] std::optional<Affine2> FloatingSelectionMap(const TransformEdit& edit) const noexcept;
    // The clipboard: Copy takes the selection (or the whole canvas) from the active layer or its mask as shown;
    // Copy Merged takes the host's composite; Paste puts the pixels back in place as a new layer; Layer via Copy
    // skips the clipboard. The host mirrors copies to the system clipboard and brings other apps' images in.
    [[nodiscard]] std::optional<GridRect> SelectionCopyRegion() const noexcept;
    [[nodiscard]] bool CanCopyPixels() const noexcept;
    [[nodiscard]] std::optional<PixelClipboard> RenderSelectedPixels(const ImageLayer& layer, bool mask) const;
    [[nodiscard]] bool CanCopyMerged() const noexcept;
    [[nodiscard]] std::optional<PixelClipboard> RenderMergedPixels(const Raster& composite) const;
    // Sealed immutable Copy/Cut preparation. Callers reserve complete capture/pixels/history/encoding/transfer
    // before capture. Copy Merged requires the full-document token captured before utility composition.
    [[nodiscard]] std::optional<ClipboardCommandInput> PrepareClipboardCommandInput(
        ClipboardCommandKind kind, const Raster& composite = {}, const std::optional<JobToken>& compositeAuthority = {},
        const std::shared_ptr<const void>& outputLifetime = {}) const;
    [[nodiscard]] static std::optional<PreparedClipboardCommand> PrepareClipboardCommandPublication(
        ClipboardCommandInput input, CancelToken cancellation = {},
        DocumentHistory::PrepareObserver historyObserver = {});
    [[nodiscard]] bool IsPreparedClipboardCommandCurrent(const PreparedClipboardCommand& prepared) const noexcept;
    [[nodiscard]] bool ApplyPreparedClipboardCommandIfCurrent(PreparedClipboardCommand& prepared) noexcept;
    [[nodiscard]] std::uint64_t ClipboardCommandMemoryBytes(ClipboardCommandKind kind,
                                                            const Raster& composite = {}) const;
    [[nodiscard]] std::uint64_t ClipboardCommandRetirementMemoryBytes(ClipboardCommandKind kind,
                                                                      const Raster& composite = {}) const;
    // Admitted owner capture after gradient/pixel-move settlement. Workers own the snapshot and never
    // consult the live private clipboard. Private origin uses its sealed serial; foreign pixels are centered.
    [[nodiscard]] std::optional<ClipboardPasteInput> PrepareClipboardPasteInput() const;
    [[nodiscard]] static std::optional<PreparedClipboardPaste> PrepareClipboardPastePublication(
        ClipboardPasteInput input, bool privateOrigin, const Raster& foreignImage = {}, CancelToken cancellation = {},
        DocumentHistory::PrepareObserver observer = {});
    [[nodiscard]] bool ApplyPreparedClipboardPasteIfCurrent(PreparedClipboardPaste& prepared) noexcept;
    [[nodiscard]] std::int64_t ClipboardPastePixelLimit() const noexcept;
    [[nodiscard]] std::uint64_t ClipboardPastePreparationMemoryBytes(std::int64_t additionalPixels) const noexcept;
    [[nodiscard]] std::uint64_t ClipboardPasteRetirementMemoryBytes() const noexcept;
    [[nodiscard]] std::uint64_t ClipboardSerial() const noexcept
    {
        return _clipboardSerial;
    }
    [[nodiscard]] Guid ClipboardSessionIdentity() const noexcept
    {
        return _tokenIdentity.value;
    }
    bool CopySelection();
    bool CopyMergedSelection(const Raster& composite);
    void CutSelection();
    [[nodiscard]] bool CanPaste() const noexcept
    {
        return _document.has_value() && _clipboard.has_value();
    }
    void Paste();
    // Pixels from another app, centered on the canvas (`Paste`).
    void PasteImage(const Raster& image);
    void LayerViaCopy();
    [[nodiscard]] std::uint64_t LayerCopyPreparationMemoryBytes() const;
    [[nodiscard]] std::optional<LayerCopyInput> PrepareLayerCopyInput() const;
    [[nodiscard]] static LayerCopyResult PrepareLayerCopyPublication(
        LayerCopyInput input, Executor& executor, CancelToken cancellation, LayerCopyObserver observer,
        DocumentHistory::PrepareObserver historyObserver = {});
    [[nodiscard]] bool ApplyPreparedLayerCopyIfCurrent(PreparedLayerCopy& prepared) noexcept;
    [[nodiscard]] const std::optional<PixelClipboard>& Clipboard() const noexcept
    {
        return _clipboard;
    }

    // Color adjustments and filters (C12, Specs/Document/Document_Tools.md): the Image menu's Curves, Levels,
    // Hue/Saturation, Exposure, Gradient Map and Grain and the Filter menu's blurs, noise and lens correction run on
    // the active image layer inside the selection, previewed live through `DisplayDocument` and recorded as one
    // entry on OK; adjustment layers keep their settings live and edit through the same panels over the composite
    // beneath them. Painting and history wait while a panel is open.
    [[nodiscard]] bool CanAdjustColors() const noexcept;
    // Content-Aware Fill (C13) needs a non-empty selection on an image layer's pixels.
    [[nodiscard]] bool CanContentAwareFill() const noexcept;
    [[nodiscard]] bool HasColorEdit() const noexcept
    {
        return _colorEdit.has_value();
    }
    [[nodiscard]] bool HasColorEdit(FilterKind kind) const noexcept
    {
        return _colorEdit.has_value() && _colorEdit->kind == kind;
    }
    [[nodiscard]] const std::optional<ColorEdit>& PendingColorEdit() const noexcept
    {
        return _colorEdit;
    }
    FilterSettings filterSettings; // the last applied settings, the next panel's starting point
    // One pipeline for every kind: Begin settles pending work and opens the panel on the active layer, Update
    // previews, Commit renders at full size and records one entry (an edit that changes nothing, or a layer that
    // changed under the panel, closes without one), Cancel drops the panel.
    void BeginColorEdit(FilterKind kind, bool deferred = false);
    void UpdateColorEdit(const FilterSettings& settings, bool preview);
    // The open Levels or Hue/Saturation panel's own settings (nothing while another kind is open).
    void UpdateLevels(const LevelsSettings& settings, bool preview);
    void UpdateHueSaturation(const HueSaturationSettings& settings, bool preview);
    // Owning immutable work; direct Commit runs the same graph with an InlineExecutor.
    [[nodiscard]] std::optional<ColorEditJobInput> PrepareColorEditCommit() const;
    // Metadata-only adjustment transactions: capture under complete admission, construct history on a worker,
    // validate authority before nonthrowing publication, and retain replaced owners for aggregate retirement.
    [[nodiscard]] std::optional<AdjustmentClosureInput> PrepareAdjustmentClosureInput(bool commit) const;
    [[nodiscard]] static std::optional<PreparedAdjustmentClosure> PrepareAdjustmentClosurePublication(
        AdjustmentClosureInput input, const std::optional<CanvasDocument>& source, DocumentHistory::StateView history,
        CancelToken cancellation = {}, DocumentHistory::PrepareObserver observer = {});
    [[nodiscard]] bool IsPreparedAdjustmentClosureCurrent(const PreparedAdjustmentClosure& prepared) const noexcept;
    [[nodiscard]] bool ApplyPreparedAdjustmentClosureIfCurrent(PreparedAdjustmentClosure& prepared) noexcept;
    [[nodiscard]] std::optional<AdjustmentOpeningInput> PrepareAdjustmentOpeningInput() const;
    [[nodiscard]] static std::optional<PreparedAdjustmentOpening> PrepareAdjustmentOpeningPublication(
        AdjustmentOpeningInput input, const std::optional<CanvasDocument>& source, Raster below,
        DocumentHistory::StateView history, CancelToken cancellation = {},
        DocumentHistory::PrepareObserver observer = {});
    [[nodiscard]] bool IsPreparedAdjustmentOpeningCurrent(const PreparedAdjustmentOpening& prepared) const noexcept;
    [[nodiscard]] bool ApplyPreparedAdjustmentOpeningIfCurrent(PreparedAdjustmentOpening& prepared) noexcept;
    [[nodiscard]] std::optional<ColorDismissalInput> PrepareColorDismissalInput() const;
    [[nodiscard]] static std::optional<PreparedColorDismissal> PrepareColorDismissalPublication(
        ColorDismissalInput input, DocumentHistory::StateView history, CancelToken cancellation = {});
    [[nodiscard]] bool IsPreparedColorDismissalCurrent(const PreparedColorDismissal& prepared) const noexcept;
    [[nodiscard]] bool ApplyPreparedColorDismissalIfCurrent(PreparedColorDismissal& prepared) noexcept;
    [[nodiscard]] std::optional<ColorEditJobInput> PrepareColorEditPreview() const;
    [[nodiscard]] bool ApplyColorEditPreview(const ColorEditPreviewResult& result);
    // Host opt-in for the complete prepared display route. Direct inline panels retain their existing path.
    void SetColorDisplayJobsEnabled(bool enabled) noexcept
    {
        _colorDisplayJobsEnabled = enabled;
    }
    [[nodiscard]] bool ColorLookupPreviewReady() const noexcept;
    [[nodiscard]] std::optional<ColorDisplayPreviewInput> PrepareColorDisplayPreviewInput(bool lookupPreview) const;
    // Requires an empty admitted destination; a populated slot preserves its captured owners.
    void PrepareColorDisplayPreviewInputInto(std::optional<ColorDisplayPreviewInput>& destination,
                                             bool lookupPreview) const;
    [[nodiscard]] std::optional<ColorDisplayPreviewMemory> ColorDisplayPreviewMemoryBytes(
        bool lookupPreview) const noexcept;
    [[nodiscard]] static ColorDisplayPreviewResult PrepareColorDisplayPreviewPublication(ColorDisplayPreviewInput input,
                                                                                         Executor& executor,
                                                                                         CancelToken cancellation,
                                                                                         ColorDisplayObserver observer);
    // Worker-only, already allocated destination; consumes input on every outcome without a large stack result.
    static void PrepareColorDisplayPreviewPublicationInto(ColorDisplayPreviewInput&& input,
                                                          ColorDisplayPreviewResult& output, Executor& executor,
                                                          CancelToken cancellation, ColorDisplayObserver observer);
    // The scheduler's named allocation-failure boundary can return captured owners without copying them.
    // This only prepares invalidation; ordinary authority checks still decide whether it may change the view.
    [[nodiscard]] static PreparedColorDisplayPreview PrepareFailedColorDisplayPreviewPublication(
        ColorDisplayPreviewInput input, ColorEditJobStatus status) noexcept;
    // Requires an empty admitted destination; a populated slot preserves it and the supplied input.
    static void PrepareFailedColorDisplayPreviewPublicationInto(ColorDisplayPreviewInput&& input,
                                                                std::optional<PreparedColorDisplayPreview>& destination,
                                                                ColorEditJobStatus status) noexcept;
    [[nodiscard]] bool ApplyPreparedColorDisplayPreviewIfCurrent(PreparedColorDisplayPreview& prepared) noexcept;
    [[nodiscard]] bool ApplyColorEditHistogram(const ColorEditHistogramResult& result);
    [[nodiscard]] std::uint64_t ColorEditSequence() const noexcept
    {
        return _colorEditSequence;
    }
    // No pixel kernels; rejects stale work, keeps failed/cancelled panels, records one successful edit.
    [[nodiscard]] bool ApplyColorEditResult(const ColorEditJobResult& result);
    [[nodiscard]] bool IsPreparedColorEditCurrent(const PreparedColorEdit& prepared) const noexcept;
    [[nodiscard]] bool ApplyPreparedColorEditResult(PreparedColorEdit& prepared) noexcept;
    void CommitColorEdit();
    void CancelColorEdit();
    void AutoLevels(LevelsAuto mode);
    void SetLevelsSampleMode(std::optional<LevelsSample> mode) noexcept;
    // The eyedroppers read the original layer under a document point; transparent pixels are ignored.
    void SampleLevels(Vec2 point);
    void SetHueSampleMode(std::optional<HueSampleMode> mode) noexcept;
    void SetHueTargeting(bool targeting) noexcept;
    // The hue under a document point of the visible composite; near-neutral pixels have no meaningful hue.
    [[nodiscard]] static std::optional<double> SampledHue(const Raster& composite, Vec2 point) noexcept;
    // The eyedroppers: re-center, widen or narrow the selected range's band on the sampled hue.
    void SampleHueRange(const Raster& composite, Vec2 point);
    // Owner metadata application of a hue computed on an admitted worker. No pixel source is read here.
    void SampleHueRangeValue(std::optional<double> hue);
    // Targeted adjustment: picks the range owning the sampled color; the drag changes its saturation (or hue).
    bool BeginHueTargeting(const Raster& composite, Vec2 point);
    [[nodiscard]] bool BeginHueTargetingValue(std::optional<double> hue);
    void DragHueTargeting(double viewDelta, bool adjustsHue);
    void EndHueTargeting() noexcept;
    // Adjustment layers: a new one above the active layer (`New <Kind> Adjustment`; a Gradient Map runs from the
    // foreground to the background color, each Grain gets its own pattern) opens for editing; `UpdateAdjustment`
    // changes a record live; editing wraps the kind's panel in one `Edit <Kind> Adjustment` entry over the
    // composite beneath the layer (`below`, rendered by the host), OK keeping the settings and Cancel restoring them.
    void AddAdjustment(AdjustmentKind kind);
    bool UpdateAdjustment(const Guid& id, const LayerAdjustment& value);
    [[nodiscard]] std::optional<Guid> AdjustmentEditingID() const noexcept
    {
        return _adjustmentEditingID;
    }
    void SetAdjustmentEditingID(std::optional<Guid> id) noexcept
    {
        _adjustmentEditingID = id;
    }
    bool BeginAdjustmentEditing(const Guid& id, const Raster& below, bool deferred = false);
    bool PreviewAdjustmentEditing(bool preview);
    bool FinishAdjustmentEditing(bool commit);
    [[nodiscard]] const std::optional<LayerAdjustment>& AdjustmentOriginal() const noexcept
    {
        return _adjustmentOriginal;
    }
    // Shift+= / Shift+-: the active layer's blend mode, forward or back, wrapping (one entry each).
    void StepBlendMode(bool forward);
    // The shape filling its box, anti-aliased where it curves, premultiplied BGRA; empty over the pixel budget.
    [[nodiscard]] static Raster ShapeImage(ShapeKind kind, int width, int height, std::uint32_t argb,
                                           double cornerRadius);
    // Inserts pixels as a new layer above the active one (inside its folder), all in one entry. Pasting drops the
    // selection, as in Photoshop; a drawn shape keeps it.
    void AddPixelLayer(const Raster& image, Vec2 origin, std::wstring name, const std::wstring& editName,
                       bool dropsSelection, std::optional<LayerShapeStyle> shape);

    // Eyedropper (C11): the straight sRGB color of the visible canvas at one document pixel, read from the host's
    // composite (document-size premultiplied BGRA). Nullopt outside the canvas or over a fully transparent pixel.
    [[nodiscard]] static std::optional<std::uint32_t> SampleCompositeColor(const Raster& composite,
                                                                           Vec2 point) noexcept;
    // The accepted first/next sample also records its exact layer/mask endpoint for later Shift lines.
    // A worker-prepared opening does not fabricate a pointer sample during publication.
    void ContinueBrush(Vec2 point);
    // Exclusive sample capture after admission. Only scalar opening metadata remains visible while it runs.
    [[nodiscard]] std::optional<StrokeSampleInput> TakeStrokeSampleInput(std::uint64_t opening, Vec2 point,
                                                                         BrushCoverageEngine*& ownerEngine) noexcept;
    [[nodiscard]] bool IsStrokeSampleCurrent(const StrokeSampleInput& input) const noexcept;
    [[nodiscard]] bool ApplyStrokeSampleIfCurrent(StrokeSampleInput& input, BrushCoverageEngine* ownerEngine) noexcept;
    [[nodiscard]] bool DiscardStrokeSampleIfCurrent(std::uint64_t opening) noexcept;
    // Replaces the provisional tail by the final curve and commits the tiles as one entry (`Brush Stroke`,
    // `Erase`, `Paint Mask`); nothing is recorded when no pixel changed.
    void FinishBrush();
    // Staged P1 closure: the caller must admit the complete original job before exclusive capture.
    [[nodiscard]] std::optional<StrokeFinishInput> TakeStrokeFinishInput(std::uint64_t opening,
                                                                         BrushCoverageEngine*& ownerEngine) noexcept;
    [[nodiscard]] bool DiscardStrokeFinishIfCurrent(std::uint64_t opening) noexcept;
    [[nodiscard]] static StrokeFinishResult PrepareStrokeFinishPublication(
        StrokeFinishInput input, CancelToken cancellation, StrokeFinishObserver observer,
        DocumentHistory::PrepareObserver historyObserver = {});
    [[nodiscard]] bool IsPreparedStrokeFinishCurrent(const PreparedStrokeFinish& prepared) const noexcept;
    [[nodiscard]] bool ApplyPreparedStrokeFinishIfCurrent(PreparedStrokeFinish& prepared) noexcept;
    void CancelBrush() noexcept;
    [[nodiscard]] const BrushStroke* PendingBrush() const noexcept
    {
        return _brush.get();
    }
    [[nodiscard]] std::optional<std::uint64_t> PendingBrushOpening() const noexcept
    {
        return _brush ? std::optional(_brushOpening)
                      : (_brushUpdatingOpening ? _brushUpdatingOpening : _brushFinishingOpening);
    }
    // Owner-only harness observation; the weak handle retains no draft or borrowed graphics owner.
    [[nodiscard]] std::weak_ptr<BrushStroke> WeakPendingBrushForTest() const noexcept
    {
        return _brush;
    }
    [[nodiscard]] BrushStroke* MutableBrush() noexcept
    {
        return _brush.get();
    }
    // The commit FinishBrush just made, once.
    [[nodiscard]] std::optional<BrushCommitInfo> TakeLastBrushCommit() noexcept
    {
        std::optional<BrushCommitInfo> result = std::move(_lastBrushCommit);
        _lastBrushCommit.reset();
        return result;
    }
    // Where a Shift-click paints a line from: the end of the last stroke while the same layer (or mask) is the
    // target.
    [[nodiscard]] std::optional<Vec2> ShiftLineStart() const noexcept;
    // ] / [ : a fifth up or down, always at least one pixel, 1–2000.
    void ChangeBrushSize(bool increase) noexcept;
    // Shift-] / Shift-[ : hardness in Photoshop's 25 % steps.
    void ChangeBrushHardness(bool increase) noexcept;
    // Photoshop-style opacity keys: 1 = 10 % … 9 = 90 %, 0 = 100 %; two digits within 0.6 s set an exact value.
    void TypeOpacityDigit(int digit, double timeSeconds) noexcept;
    // The percent a digit types (with the two-digit window), for a caller that applies it elsewhere; -1 when none.
    [[nodiscard]] int TypedOpacityPercent(int digit, double timeSeconds) noexcept;
    // A tool switch ends a two-digit entry in progress.
    void ClearPendingOpacityDigit() noexcept
    {
        _pendingOpacityDigit.reset();
    }
    // The pixel budget left for a raster edit of `layer` (100 M image or mask pixels less the other layers).
    [[nodiscard]] std::int64_t RasterEditPixelLimit(const ImageLayer& layer, bool mask) const noexcept;
    // The document's 100 M image (or mask) pixel budget left once every layer but those in `excluding` is counted.
    // Decision D7: every edit that grows pixels (brush and raster edits, pastes, shapes, duplicates, masks, the
    // floating selection's merge, filters that grow the layer, merges, distortions) checks it, so a document never
    // outgrows what it can save.
    [[nodiscard]] std::int64_t PixelBudgetLeft(bool mask, std::initializer_list<Guid> excluding = {}) const noexcept;
    [[nodiscard]] std::int64_t PixelBudgetLeft(bool mask, const std::unordered_set<Guid>& excluding) const noexcept;
    // True once after an edit was refused because it would take the document past its 100 M image or mask pixel
    // budget (Decision D7); the host says so (IDS_ERROR_TOO_LARGE).
    [[nodiscard]] bool TakeBudgetRefusal() noexcept
    {
        const bool refused = _budgetRefused;
        _budgetRefused = false;
        return refused;
    }
    BrushSettings brushSettings;
    BrushToolMode brushMode = BrushToolMode::Paint;
    bool maskPaintWhite = false;
    SpotHealingMode spotHealingMode = SpotHealingMode::ContentAware;
    CloneSettings cloneSettings;
    std::optional<Vec2> cloneSource;
    SmearMode smearMode = SmearMode::Liquify;
    GradientSettings gradientSettings;
    ShapeKind shapeKind = ShapeKind::Rectangle;
    // Corner radius in pixels for rectangles the Shape tool draws; 0 keeps the corners square.
    double shapeCornerRadius = 0;

    // Palette (foreground/background paint colors, opaque ARGB). Session state outside history, as in the legacy
    // EditorSession brush settings: X swaps, D resets. A targeted mask has its own black/white palette (legacy
    // ColorPalette.swift): there X flips `maskPaintWhite` and D resets it to black, leaving the colors alone.
    static constexpr uint32_t kDefaultForegroundArgb = 0xFF000000u;
    static constexpr uint32_t kDefaultBackgroundArgb = 0xFFFFFFFFu;
    [[nodiscard]] uint32_t ForegroundColor() const noexcept
    {
        return _foregroundArgb;
    }
    [[nodiscard]] uint32_t BackgroundColor() const noexcept
    {
        return _backgroundArgb;
    }
    struct PaletteSampleAuthority final
    {
        Guid session;
        std::uint64_t revision = 0;
        bool operator==(const PaletteSampleAuthority&) const noexcept = default;
    };
    struct PaletteIntentAuthority final
    {
        Guid session;
        std::uint64_t revision = 0;
        bool operator==(const PaletteIntentAuthority&) const noexcept = default;
    };
    [[nodiscard]] PaletteSampleAuthority CapturePaletteSampleAuthority() const noexcept
    {
        return {_tokenIdentity.value, _paletteRevision.Current()};
    }
    // Explicit palette intent stays stable across this sampler's own publications. Pending points and
    // younger captured tools bind this authority separately from the per-publication revision above.
    [[nodiscard]] PaletteIntentAuthority CapturePaletteIntentAuthority() const noexcept
    {
        return {_tokenIdentity.value, _paletteIntentRevision.Current()};
    }
    [[nodiscard]] bool IsPaletteIntentCurrent(PaletteIntentAuthority authority) const noexcept
    {
        return authority.session == _tokenIdentity.value && _paletteIntentRevision.Matches(authority.revision);
    }
    // A sampled publication renews only its own authority. Explicit equal-value/A-B-A palette input,
    // session copy/move, swap or reset prevents an older asynchronous sample from publishing.
    [[nodiscard]] bool ApplySampledForegroundColor(PaletteSampleAuthority& authority, uint32_t argb) noexcept
    {
        if (authority.session != _tokenIdentity.value || !_paletteRevision.Matches(authority.revision))
            return false;
        _paletteRevision.Advance();
        _foregroundArgb = argb | 0xFF000000u;
        ApplyForegroundToBrush();
        RefreshGradient();
        authority = CapturePaletteSampleAuthority();
        return true;
    }
    [[nodiscard]] bool ApplySampledBackgroundColor(PaletteSampleAuthority& authority, uint32_t argb) noexcept
    {
        if (authority.session != _tokenIdentity.value || !_paletteRevision.Matches(authority.revision))
            return false;
        _paletteRevision.Advance();
        _backgroundArgb = argb | 0xFF000000u;
        RefreshGradient();
        authority = CapturePaletteSampleAuthority();
        return true;
    }
    void SetForegroundColor(uint32_t argb) noexcept
    {
        _paletteIntentRevision.Advance();
        _paletteRevision.Advance();
        _foregroundArgb = argb | 0xFF000000u;
        ApplyForegroundToBrush();
        RefreshGradient();
    }
    void SetBackgroundColor(uint32_t argb) noexcept
    {
        _paletteIntentRevision.Advance();
        _paletteRevision.Advance();
        _backgroundArgb = argb | 0xFF000000u;
        RefreshGradient();
    }
    void SwapPaletteColors() noexcept
    {
        _paletteIntentRevision.Advance();
        _paletteRevision.Advance();
        if (_maskSelected)
        {
            maskPaintWhite = !maskPaintWhite;
        }
        else
        {
            std::swap(_foregroundArgb, _backgroundArgb);
            ApplyForegroundToBrush();
        }
        RefreshGradient();
    }
    void ResetPaletteColors() noexcept
    {
        _paletteIntentRevision.Advance();
        _paletteRevision.Advance();
        if (_maskSelected)
        {
            maskPaintWhite = false;
        }
        else
        {
            _foregroundArgb = kDefaultForegroundArgb;
            _backgroundArgb = kDefaultBackgroundArgb;
            ApplyForegroundToBrush();
        }
        RefreshGradient();
    }

  private:
    [[nodiscard]] std::optional<WandLayerSelectionInput> PrepareWandSelectionInput(Vec2 point, WandSettings settings,
                                                                                   bool antialiased, SelectionMode mode,
                                                                                   bool composite,
                                                                                   bool preservePanel = false) const;
    [[nodiscard]] static std::optional<PreparedWandLayerSelection> PrepareWandSelectionPublication(
        WandLayerSelectionInput input, Raster composite, WandFailure* failure, CancelToken cancellation,
        DocumentHistory::PrepareObserver historyObserver, const WandLayerSelectionObserver* observer,
        std::shared_ptr<const SharedPublicationMemory> memory);
    [[nodiscard]] std::optional<WandLayerSelectionMemoryPlan> PlanWandSelectionMemory(
        Vec2 point, WandSettings settings, bool composite, SelectionMode mode,
        bool preservePanel = false) const noexcept;
    [[nodiscard]] bool PanelWandEligible() const noexcept;

    [[nodiscard]] SelectionCommandAuthority CaptureSelectionCommandAuthority() const noexcept;
    [[nodiscard]] bool CanApplySelectionCommand(const SelectionCommandAuthority& authority) const noexcept;
    [[nodiscard]] bool HasDeferredTransform() const noexcept
    {
        return _transformEdit && _transformEdit->deferred;
    }
    // A session copy/move is a different owner even if all document counters match. Moves also retire the
    // source identity because a moved-from optional document can remain engaged with moved-out layer storage.
    struct TokenIdentity
    {
        Guid value = Guid::Generate();
        TokenIdentity() = default;
        TokenIdentity(const TokenIdentity&) noexcept {}
        TokenIdentity(TokenIdentity&& source) noexcept
        {
            source.value = Guid::Generate();
        }
        TokenIdentity& operator=(const TokenIdentity&) noexcept
        {
            value = Guid::Generate();
            return *this;
        }
        TokenIdentity& operator=(TokenIdentity&& source) noexcept
        {
            value = Guid::Generate();
            source.value = Guid::Generate();
            return *this;
        }
    };
    // The brush paints these channels. The palette foreground is that color, as in the legacy session.
    void ApplyForegroundToBrush() noexcept
    {
        brushSettings.red = static_cast<double>((_foregroundArgb >> 16) & 0xFFu) / 255.0;
        brushSettings.green = static_cast<double>((_foregroundArgb >> 8) & 0xFFu) / 255.0;
        brushSettings.blue = static_cast<double>(_foregroundArgb & 0xFFu) / 255.0;
    }
    // True when Undo/Redo should not move history: an open color panel, or a pending edit that this call drops.
    [[nodiscard]] bool CancelPendingEdit();
    void Restore(DocumentHistory::Snapshot snapshot);
    // Every change of the active layer goes through here (legacy EditorSession.activeLayerID didSet): another layer
    // drops the mask target, and the selection becomes that layer alone. A mask stays targeted only while the
    // active layer has one.
    void SetActiveLayerID(const std::optional<Guid>& id);
    static void AdoptClipping(const Guid& id, std::span<ImageLayer> layers);
    static void ReleaseDetachedClipping(std::span<ImageLayer> layers);
    [[nodiscard]] const ImageLayer* FindLayer(const Guid& id) const noexcept;
    void NoteDocumentMutation() noexcept
    {
        ++_generation;
        ++_editRevision;
    }
    [[nodiscard]] bool BeginGradientImpl(Vec2 point, bool deferred);
    [[nodiscard]] std::optional<LayerMetadataInput> PrepareLayerMetadataInputImpl(LayerMetadataKind kind, Guid target,
                                                                                  std::wstring_view name,
                                                                                  double opacity,
                                                                                  LayerBlendMode blendMode,
                                                                                  bool preserve) const;
    [[nodiscard]] std::uint64_t LayerMetadataMemoryBytesImpl(LayerMetadataKind kind, std::size_t nameUnits,
                                                             bool preserve) const noexcept;
    [[nodiscard]] bool BeginPixelMoveImpl(bool duplicate, bool deferred);
    [[nodiscard]] bool ApplyRasterEditImpl(ImageLayer&& prepared, bool mask, std::wstring name, const JobToken& token,
                                           bool gradientCommit, bool pixelMoveCommit = false,
                                           DocumentSelection* movedSelection = nullptr,
                                           PreparedRasterEdit* publication = nullptr);
    [[nodiscard]] bool ApplyColorEditResultImpl(const ColorEditJobResult& result, PreparedColorEdit* prepared);

    TokenIdentity _tokenIdentity;
    std::optional<CanvasDocument> _document;
    std::optional<Guid> _activeLayerID;
    std::unordered_set<Guid> _selected;
    std::unordered_set<Guid> _collapsed;
    InputRevision _collapseRevision;
    uint32_t _foregroundArgb = kDefaultForegroundArgb;
    uint32_t _backgroundArgb = kDefaultBackgroundArgb;
    InputRevision _paletteRevision;
    InputRevision _paletteIntentRevision;
    std::optional<TransformEdit> _transformEdit;
    std::optional<std::pair<Guid, Guid>> _transformDuplicate; // copy, source
    std::optional<CropRect> _cropRect;
    SnapGuides _snapGuides;
    bool _maskSelected = false;
    std::uint64_t _generation = 0;
    std::uint64_t _documentSerial = 0;
    std::uint64_t _restoreSerial = 0;
    std::uint64_t _editRevision = 0;
    mutable std::uint64_t _displayRevision = 0;
    mutable std::uint64_t _displayTransformOpening = 0, _displaySourceGeneration = 0;
    bool _colorDisplayJobsEnabled = false;
    mutable std::uint64_t _displayColorOpening = 0, _displayColorSequence = 0;
    mutable std::optional<CanvasDocument> _displayCopy;
    std::optional<LassoDraft> _lassoDraft;
    std::uint64_t _lassoOpening = 0;
    std::uint64_t _lassoOpeningIntentSerial = 0;
    std::uint64_t _lassoOpeningPendingSerial = 0;
    std::uint64_t _lassoClosedOpening = 0, _lassoClosedRevision = 0;
    const void* _lassoClosedArenaIdentity = nullptr;
    std::shared_ptr<const BoundedLassoGesture> _lassoClosedWriter;
    std::optional<DocumentReadAuthority> _lassoClosedSource;
    std::optional<Guid> _lassoClosedActive;
    std::optional<CropRect> _lassoClosedCrop;
    bool _lassoClosedMask = false;
    std::optional<DocumentHistory::StateView> _lassoClosedHistory;
    void InvalidateLassoPendingIntents() noexcept;
    [[nodiscard]] bool CanApplyHistoryRestoreWithoutLasso(const HistoryRestoreAuthority& authority) const noexcept;
    [[nodiscard]] std::optional<LassoOpeningInput> CaptureLassoOpeningInput(Vec2 point, SelectionMode mode,
                                                                            LassoKind kind, bool marquee,
                                                                            std::optional<bool> capturedAntialiased);
    std::uint64_t _lassoFinishSerial = 0;
    std::uint64_t _lassoFinishPendingSerial = 0;
    std::optional<DocumentSelection> _selectionMoveOrigin;
    std::optional<Vec2> _selectionMoveOffset;
    std::uint64_t _selectionMoveOpening = 0;
    std::shared_ptr<BrushStroke> _brush;
    std::uint64_t _brushOpening = 0;
    std::optional<std::uint64_t> _brushFinishingOpening;
    std::optional<std::uint64_t> _brushUpdatingOpening;
    BrushCoverageEngine* _brushEngine = nullptr;
    struct LastBrushPoint
    {
        Vec2 point;
        Guid layerID;
        bool mask = false;
    };
    std::optional<LastBrushPoint> _lastBrushPoint;
    std::optional<std::pair<int, double>> _pendingOpacityDigit;
    std::optional<BrushCommitInfo> _lastBrushCommit;
    std::optional<Vec2> _cloneOffset;
    std::shared_ptr<WarpStroke> _warp;
    std::uint64_t _warpOpening = 0;
    std::optional<std::uint64_t> _warpUpdatingOpening;
    std::optional<std::uint64_t> _warpUpdatingSequence;
    std::optional<std::uint64_t> _warpFinishingOpening;
    struct WarpSource
    {
        Guid layerID;
        Raster image;
        LayerTransform transform;
        std::thread::id ownerThread{};
    };
    WarpSource _warpSource;
    std::array<std::array<double, 3>, 3> _parkedTips{{{40, 1, 1}, {40, 0, 1}, {40, 0, 1}}};
    std::uint32_t _healSeed = 0x9e3779b9u;
    std::optional<GradientEdit> _gradient;
    std::uint64_t _gradientOpening = 0;
    std::optional<ShapeDraft> _shapeDraft;
    std::uint64_t _shapeOpening = 0;
    std::optional<PixelMove> _pixelMove;
    std::uint64_t _pixelMoveOpening = 0;
    std::optional<PixelClipboard> _clipboard;
    std::uint64_t _clipboardSerial = 1;
    [[nodiscard]] bool CanPrepareClipboardCommand(ClipboardCommandKind kind) const noexcept;
    [[nodiscard]] std::optional<CopyPixelsInput> ClipboardPixelsInput(ClipboardCommandKind kind,
                                                                      const Raster& composite) const;
    std::optional<ColorEdit> _colorEdit;
    std::uint64_t _colorEditSequence = 0;
    bool _budgetRefused = false;
    std::optional<Guid> _adjustmentEditingID;
    std::optional<LayerAdjustment> _adjustmentOriginal;
    struct PreviewLayer
    {
        Guid layerID;
        Raster raster;
        LayerTransform transform;
    };
    [[nodiscard]] PreviewLayer PreviewedLayer() const noexcept;
    // The open panel when it is of `kind`.
    [[nodiscard]] ColorEdit* OpenColorEdit(FilterKind kind) noexcept;
    [[nodiscard]] std::optional<LayerAdjustment> EditedAdjustment() const;
    bool GrowFilterForBlur();
    [[nodiscard]] bool HoldColorDisplayOwners() const noexcept;
    void InvalidateColorPreviewOwners() noexcept;
    // Pads the filter's grid out to cover `extent` (layer pixels), keeping what is already grown.
    bool GrowFilterGrid(int left, int top, int right, int bottom);
    void RenderColorEditInline();
    void MeasureColorEditHistogramInline();
    [[nodiscard]] bool ColorEditOpeningMatches(const JobToken& token, std::uint64_t opening,
                                               const Raster& original) const noexcept;
    [[nodiscard]] bool HasCurrentDeferredSelectionMoveSource() const noexcept;
    [[nodiscard]] bool HasCurrentWandColorSelectionBinding() const noexcept;
    void NormalizeWandColorSelectionToken(JobToken& token) const noexcept;
    [[nodiscard]] bool IsJobTargetAndSessionCurrent(const JobToken& token, std::uint64_t latestSequence) const noexcept;
    // A brush stroke, gradient or Invert/pixel-move edit on `layer` (or its mask): built, validated, pixel-budget
    // limited and clipped to the selection, or null when construction fails, the raster is invalid, or there is no
    // document. `settings` is the brush the caller paints with; edits that only need a raster to write into (a
    // gradient, Invert, a pixel move) leave it default. `kind` becomes the stroke's own BrushKind (Erasing() /
    // Healing()); the current spotHealingMode always travels with it.
    [[nodiscard]] std::shared_ptr<BrushStroke> MakeRasterEdit(const ImageLayer& layer, bool mask,
                                                              const BrushSettings& settings = {},
                                                              BrushKind kind = BrushKind::Paint) const;
    // What Clone Stamp and the Wand each read at document size: `allLayers` reads `composite` (null or wrongly
    // sized reads nothing), else the active layer's own pixels at its displayed transform; a folder or blank layer
    // reads as transparent.
    [[nodiscard]] Raster LayerSample(bool allLayers, const Raster* composite) const;
    // "`prefix`1", "`prefix`2", … skipping names already in the document (`NextShapeName`, `NextLayerName`, a new
    // folder or group).
    [[nodiscard]] std::wstring UniqueLayerName(const std::wstring& prefix) const;
    void MergeFloating(const TransformEdit& edit, const FloatingTransform& floating);
    void CancelFloating(const FloatingTransform& floating);
    void InsertLayerAboveActive(ImageLayer layer, const std::wstring& editName, bool dropsSelection);
    // A shape layer scaled to a new size draws its shape again at that size, so a rounded corner keeps its radius
    // instead of stretching. Part of the edit that changed the size.
    void RedrawShape(ImageLayer& layer, std::span<const ImageLayer> records);
    // Installs a raster edit as one entry; `selectionAfterCommit` replaces the selection inside the same entry (a
    // moved selection). False when nothing was installed (no pixels, or the layer changed under the edit).
    bool CommitStroke(const std::shared_ptr<BrushStroke>& stroke,
                      const std::optional<DocumentSelection>& selectionAfterCommit = std::nullopt);
    [[nodiscard]] std::uint32_t NextHealSeed() noexcept
    {
        _healSeed = _healSeed * 1664525u + 1013904223u;
        return _healSeed;
    }
};
