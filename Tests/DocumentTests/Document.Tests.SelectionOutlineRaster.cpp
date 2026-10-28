#include "Document.Tests.SelectionOutlineRaster.h"

#include "Common/Executor.h"
#include "Common/SelectionOutlineRasterWork.h"
#include "Common/SharedPublicationMemory.h"

#include <objbase.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <stop_token>
#include <thread>
#include <vector>

namespace
{
unsigned int gPassed = 0;
unsigned int gFailed = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s selection-outline-raster.%s\n", condition ? "PASS" : "FAIL", name);
    condition ? ++gPassed : ++gFailed;
}

class ComApartment final
{
  public:
    ComApartment() noexcept : _result(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
    ~ComApartment()
    {
        if (SUCCEEDED(_result))
            CoUninitialize();
    }
    [[nodiscard]] bool Ready() const noexcept
    {
        return SUCCEEDED(_result);
    }

  private:
    HRESULT _result;
};

using Raster = PreparedSelectionOutlineRaster;
using Grid = Raster::GridKey;

[[nodiscard]] Grid BaseGrid() noexcept
{
    return Grid{64, 64, 1.0, 8.0, 8.0, 96.0f};
}

[[nodiscard]] SelectionLoops Rectangle()
{
    return SelectionLoops{std::vector<SelectionLoop>{
        {{8.0, 8.0}, {56.0, 8.0}, {56.0, 56.0}, {8.0, 56.0}},
    }};
}

struct RasterOwner final
{
    RasterOwner() : strong(SharedPublicationMemory::CreateForWork()), weak(strong) {}
    std::shared_ptr<const SharedPublicationMemory> strong;
    std::weak_ptr<const SharedPublicationMemory> weak;
};

struct LifetimeTrace final
{
    unsigned int destructions = 0;
    std::thread::id destructionThread{};
};

struct LifetimeMarker final
{
};

struct LifetimeMarkerDeleter final
{
    LifetimeTrace* trace = nullptr;

    void operator()(LifetimeMarker* marker) const noexcept
    {
        trace->destructionThread = std::this_thread::get_id();
        ++trace->destructions;
        const std::unique_ptr<LifetimeMarker> owned{marker};
    }
};

[[nodiscard]] bool HResultIs(std::int32_t actual, HRESULT expected) noexcept
{
    return actual == static_cast<std::int32_t>(expected);
}

[[nodiscard]] bool NearByte(std::uint8_t actual, int expected, int tolerance = 1) noexcept
{
    return std::abs(static_cast<int>(actual) - expected) <= tolerance;
}

[[nodiscard]] std::uint8_t At(const Raster& raster, std::size_t plane, std::uint32_t x, std::uint32_t y) noexcept
{
    const auto bytes = raster.Plane(plane);
    const auto& key = raster.Key();
    return bytes.size() == static_cast<std::size_t>(key.width) * key.height && x < key.width && y < key.height
               ? bytes[static_cast<std::size_t>(y) * key.width + x]
               : std::uint8_t{0};
}

[[nodiscard]] bool PrepareRectangle(Raster& output, Grid grid = BaseGrid())
{
    RasterOwner memory;
    const auto source = Rectangle();
    return HResultIs(Raster::Prepare(source, grid, memory.strong, {}, {}, output), S_OK);
}

void TestLiteralCoverageAndDashPhases()
{
    Raster raster;
    Expect(PrepareRectangle(raster), "literal-closed-rectangle-prepares");
    if (!raster.HasStorage())
        return;

    Expect(raster.Key() == BaseGrid(), "literal-grid-key-retained-exactly");
    Expect(raster.Plane(Raster::PlaneCount).empty(), "out-of-range-plane-read-is-empty");
    Expect(NearByte(At(raster, 0, 32, 7), 128) && NearByte(At(raster, 0, 32, 8), 128),
           "solid-top-edge-straddles-rows-seven-and-eight-at-half-coverage");
    Expect(At(raster, 0, 0, 0) == 0, "transparent-origin-remains-clear");

    // Independent reference: the closed rectangle's top edge starts at (8,8), so these points
    // are respectively 2, 6, 10, and 14 document units along its first straight segment. All are
    // at least two units from a corner/cap. The literal phase table expresses a 4-on/4-off arc;
    // plane zero is solid; dashed plane p has offset p-1. Columns are x=10,14,18,22 at row 8.
    constexpr std::array<std::array<std::uint8_t, 4>, Raster::PlaneCount> reference{{
        {{128, 128, 128, 128}}, // solid plane
        {{128, 0, 128, 0}},     // dash offset 0: arc [2,3] lies inside [0,4)
        {{128, 0, 128, 0}},     // dash offset 1: arc [3,4] is fully covered
        {{0, 128, 0, 128}},     // dash offset 2: arc [4,5] lies inside the gap
        {{0, 128, 0, 128}},     // dash offset 3
        {{0, 128, 0, 128}},     // dash offset 4: complement of offset zero
        {{0, 128, 0, 128}},     // dash offset 5
        {{128, 0, 128, 0}},     // dash offset 6
        {{128, 0, 128, 0}},     // dash offset 7
    }};
    constexpr std::array<std::uint32_t, 4> sampleX{10, 14, 18, 22};
    bool allPhasesMatch = true;
    for (std::size_t phase = 0; phase < reference.size(); ++phase)
        for (std::size_t sample = 0; sample < sampleX.size(); ++sample)
        {
            const int expected = reference[phase][sample];
            constexpr int tolerance = 1;
            allPhasesMatch = allPhasesMatch && NearByte(At(raster, phase, sampleX[sample], 8), expected, tolerance);
        }
    Expect(allPhasesMatch, "all-eight-dash-phases-match-independent-literal-perimeter-reference");
    Expect(NearByte(At(raster, 0, 10, 8), 128) && NearByte(At(raster, 1, 10, 8), 128) &&
               NearByte(At(raster, 2, 10, 8), 128) && At(raster, 3, 10, 8) <= 1,
           "x-ten-solid-and-offset-zero-and-one-on-offset-two-off");
}

void TestTranslationScaleAndDpi()
{
    Raster raster;
    const Grid grid{128, 128, 2.0, 10.0, 12.0, 192.0f};
    Expect(PrepareRectangle(raster, grid), "translated-doubled-grid-prepares");
    if (!raster.HasStorage())
        return;

    // The top edge maps to y=12 in device pixels and has a two-pixel physical stroke.
    // x=42 maps to document x=24, safely away from all corners and dash caps.
    Expect(At(raster, 0, 42, 11) >= 254 && At(raster, 0, 42, 12) >= 254,
           "double-dpi-and-document-scale-double-stroke-width-at-translated-top-edge");
    Expect(At(raster, 0, 0, 0) == 0 && At(raster, 0, 42, 11) >= 254, "translated-geometry-leaves-old-origin-clear");
    Expect(raster.Key().originPixelX == 10.0 && raster.Key().originPixelY == 12.0 &&
               raster.Key().documentScalePixel == 2.0 && raster.Key().dpi == 192.0f,
           "translated-scale-and-dpi-key-retained");

    // A 4-unit dash at 2 device pixels per DIP spans 8 pixels. These top-edge samples sit
    // inside alternating dash/gap interiors, never at a cap or corner.
    Expect(At(raster, 1, 14, 12) > 250 && At(raster, 1, 22, 12) <= 1, "dash-length-scales-with-device-transform");
}

struct CancelAtWork final
{
    CancelSource* source = nullptr;
    std::uint64_t target = 64;
    unsigned int calls = 0;
    std::uint64_t last = 0;

    static void Checkpoint(void* context, std::uint64_t work) noexcept
    {
        auto& self = *static_cast<CancelAtWork*>(context);
        ++self.calls;
        self.last = work;
        if (work == self.target)
            static_cast<void>(self.source->Cancel());
    }
};

[[nodiscard]] SelectionLoops ManyPointRectangle()
{
    SelectionLoop points;
    points.reserve(130);
    for (std::uint32_t i = 0; i < 130; ++i)
        points.push_back(Vec2{8.0 + static_cast<double>(i % 49), 8.0 + static_cast<double>(i / 49)});
    return SelectionLoops{std::vector<SelectionLoop>{std::move(points)}};
}

void TestRefusalCancellationAndOutputTransaction()
{
    Raster existing;
    Expect(PrepareRectangle(existing), "transaction-control-raster-prepares");
    if (!existing.HasStorage())
        return;
    const auto oldKey = existing.Key();
    const auto oldIdentity = existing.SourceIdentity();
    const auto oldWeakStorage = existing.WeakStorageForTest();
    const auto oldPlanePointer = existing.Plane(0).data();
    const auto oldByte = At(existing, 0, 32, 7);

    RasterOwner owner;
    const auto invalidGrid = Grid{0, 64, 1.0, 8.0, 8.0, 96.0f};
    const auto invalidGridResult = Raster::Prepare(Rectangle(), invalidGrid, owner.strong, {}, {}, existing);
    Expect(HResultIs(invalidGridResult, E_INVALIDARG) && existing.Key() == oldKey &&
               existing.SourceIdentity() == oldIdentity &&
               existing.WeakStorageForTest().lock() == oldWeakStorage.lock() &&
               existing.Plane(0).data() == oldPlanePointer && At(existing, 0, 32, 7) == oldByte,
           "invalid-grid-refusal-leaves-caller-output-byte-for-byte-published-value");

    std::stop_source preStop;
    static_cast<void>(preStop.request_stop());
    const auto preCancelled =
        Raster::Prepare(Rectangle(), BaseGrid(), owner.strong, CancelToken{preStop.get_token()}, {}, existing);
    Expect(HResultIs(preCancelled, HRESULT_FROM_WIN32(ERROR_CANCELLED)) && existing.SourceIdentity() == oldIdentity &&
               existing.Plane(0).data() == oldPlanePointer,
           "pre-cancel-refusal-preserves-existing-output");

    CancelSource cancel;
    CancelAtWork observer{&cancel};
    const auto source = ManyPointRectangle();
    const auto identity = source.Identity();
    const auto midCancelled = Raster::Prepare(source, BaseGrid(), owner.strong, cancel.Token(),
                                              Raster::Observer{&observer, CancelAtWork::Checkpoint}, existing);
    Expect(HResultIs(midCancelled, HRESULT_FROM_WIN32(ERROR_CANCELLED)) && observer.calls == 2 && observer.last == 64 &&
               source.Identity() == identity && source.PointCount() == 130,
           "midpoint-cancel-discards-preparation-and-retains-caller-source");
    Expect(existing.SourceIdentity() == oldIdentity && existing.Plane(0).data() == oldPlanePointer &&
               At(existing, 0, 32, 7) == oldByte,
           "midpoint-cancel-leaves-existing-output-unchanged");

    const std::shared_ptr<const SharedPublicationMemory> emptyOwner;
    const auto refusedOwner = Raster::Prepare(Rectangle(), BaseGrid(), emptyOwner, {}, {}, existing);
    Expect(HResultIs(refusedOwner, E_INVALIDARG) && existing.SourceIdentity() == oldIdentity &&
               existing.Plane(0).data() == oldPlanePointer,
           "unowned-output-memory-refusal-preserves-existing-output");
}

void TestPlanMemoryBoundariesAndUnchangedPlan()
{
    Raster::MemoryPlan plan{11, 22, 33, 44};
    const auto original = plan;
    const auto invalid = Grid{64, 64, 0.0, 8.0, 8.0, 96.0f};
    Expect(HResultIs(Raster::PlanMemory(invalid, 1, 4, plan), E_INVALIDARG) &&
               plan.outputBytes == original.outputBytes && plan.temporaryBytes == original.temporaryBytes &&
               plan.totalBytes == original.totalBytes &&
               plan.opaqueD2DAllowanceBytes == original.opaqueD2DAllowanceBytes,
           "invalid-scale-refusal-preserves-caller-memory-plan");

    const double nan = (std::numeric_limits<double>::quiet_NaN)();
    const double infinity = (std::numeric_limits<double>::infinity)();
    const std::array<Grid, 12> invalidGrids{{
        Grid{0, 64, 1.0, 8.0, 8.0, 96.0f},
        Grid{64, 0, 1.0, 8.0, 8.0, 96.0f},
        Grid{16385, 64, 1.0, 8.0, 8.0, 96.0f},
        Grid{64, 64, nan, 8.0, 8.0, 96.0f},
        Grid{64, 64, 1.0, infinity, 8.0, 96.0f},
        Grid{64, 64, 1.0, 8.0, nan, 96.0f},
        Grid{64, 64, 1.0, 8.0, 8.0, 0.0f},
        Grid{64, 64, 1.0, 8.0, 8.0, infinity},
        Grid{16384, 8192, 1.0, 0.0, 0.0, 96.0f},
        Grid{64, 64, (std::numeric_limits<double>::denorm_min)(), 0.0, 0.0, 96.0f},
        Grid{64, 64, (std::numeric_limits<double>::max)(), 0.0, 0.0, 96.0f},
        Grid{64, 64, 1.0, 0.0, 0.0, (std::numeric_limits<float>::denorm_min)()},
    }};
    bool rejectedAllInvalid = true;
    for (const auto& grid : invalidGrids)
        rejectedAllInvalid = rejectedAllInvalid && HResultIs(Raster::PlanMemory(grid, 1, 4, plan), E_INVALIDARG);
    Expect(rejectedAllInvalid, "zero-nonfinite-nonrepresentable-oversized-and-over-pixel-budget-grids-refuse");

    const auto ordinary = Raster::PlanMemory(Grid{128, 128, 1.0, 0.0, 0.0, 96.0f}, 1, 4, plan);
    Expect(HResultIs(ordinary, S_OK) && plan.outputBytes > 0 && plan.temporaryBytes > 0 &&
               plan.totalBytes == plan.outputBytes + plan.temporaryBytes && plan.opaqueD2DAllowanceBytes > 0,
           "finite-128-square-grid-has-complete-memory-plan");

    const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    const auto arithmeticOverflow = Raster::PlanMemory(Grid{1, 1, 1.0, 0.0, 0.0, 96.0f}, 1, maximum, plan);
    Expect(HResultIs(arithmeticOverflow, HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW)),
           "huge-point-count-arithmetic-overflow-refuses");
    Expect(HResultIs(Raster::PlanMemory(Grid{1, 1, 1.0, 0.0, 0.0, 96.0f}, maximum, maximum, plan),
                     HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW)),
           "huge-loop-count-arithmetic-overflow-refuses");
    Expect(HResultIs(Raster::PlanMemory(Grid{1, 1, 1.0, 0.0, 0.0, 96.0f}, 2, 1, plan), E_INVALIDARG),
           "more-loops-than-points-refuses");
}

void TestSourceRetentionAndWeakControlLifetime()
{
    // This fixture checks only the publication wrapper's source and control-block lifetime. It does
    // not create a scheduler ticket, bind a reservation, or claim admission/accounting coverage.
    ComApartment apartment;
    Expect(apartment.Ready(), "lifetime-fixture-com-apartment-initialized");
    if (!apartment.Ready())
        return;

    LifetimeTrace trace;
    auto uniqueMarker = std::make_unique<LifetimeMarker>();
    std::shared_ptr<LifetimeMarker> marker{uniqueMarker.release(), LifetimeMarkerDeleter{&trace}};
    std::weak_ptr<LifetimeMarker> weakMarker = marker;
    std::shared_ptr<const void> owningNullAlias{marker, nullptr};
    Expect(!owningNullAlias && owningNullAlias.use_count() > 0, "source-lifetime-fixture-is-an-owning-null-alias");
    auto adopted = SelectionLoops::AdoptForWork(Rectangle().Get(), {}, owningNullAlias);
    Expect(adopted.has_value(), "counted-lifetime-source-adopts-literal-outline");
    if (!adopted)
        return;
    SelectionLoops source = std::move(adopted.value());
    owningNullAlias.reset();
    marker.reset();
    const auto sameLookingSource = Rectangle();
    const void* const sourceIdentity = source.Identity();
    Expect(sourceIdentity != sameLookingSource.Identity() && source.Get() == sameLookingSource.Get(),
           "equal-looking-source-values-have-distinct-storage-identities");
    RasterOwner memory;
    const auto weakMemory = memory.weak;
    Raster raster;
    const auto result = Raster::Prepare(source, BaseGrid(), memory.strong, {}, {}, raster);
    Expect(HResultIs(result, S_OK) && raster.SourceIdentity() == sourceIdentity && raster.Source().size() == 1 &&
               raster.Source()[0].size() == 4 && raster.Source()[0][0] == Vec2{8.0, 8.0} &&
               raster.Source()[0][1] == Vec2{56.0, 8.0} && raster.Source()[0][2] == Vec2{56.0, 56.0} &&
               raster.Source()[0][3] == Vec2{8.0, 56.0},
           "published-raster-retains-source-identity-and-exact-literal-vertices");
    if (!raster.HasStorage())
        return;

    auto weakStorage = raster.WeakStorageForTest();
    auto copy = raster;
    Expect(copy.Plane(0).data() == raster.Plane(0).data() && copy.SourceIdentity() == sourceIdentity,
           "immutable-raster-copy-shares-source-and-plane-storage");
    source = SelectionLoops{};
    memory.strong.reset();
    Expect(!weakMemory.expired() && !weakMarker.expired() && copy.SourceIdentity() == sourceIdentity,
           "published-copy-retains-source-and-counted-owner-after-caller-reset");

    raster = Raster{};
    Expect(!weakStorage.expired() && copy.HasStorage(), "second-immutable-reader-retains-shared-storage");
    copy = Raster{};
    Expect(weakStorage.expired() && !weakMemory.expired() && weakMarker.expired() && trace.destructions == 1 &&
               trace.destructionThread == std::this_thread::get_id(),
           "source-owner-destroys-on-last-publication-reader-exactly-once");
    Expect(!weakMemory.expired(), "storage-weak-observer-expires-while-memory-weak-control-still-retains-owner");
    // weakStorage itself is the last weak observer for the publication control block; release it
    // before checking that the separate accounting owner's weak control block can expire.
    weakStorage.reset();
    Expect(weakMemory.expired(), "last-owner-weak-reset-releases-memory-control");
}
} // namespace

int RunSelectionOutlineRasterTests()
{
    gPassed = 0;
    gFailed = 0;
    ComApartment apartment;
    Expect(apartment.Ready(), "test-runner-com-apartment-initialized");
    if (apartment.Ready())
    {
        TestLiteralCoverageAndDashPhases();
        TestTranslationScaleAndDpi();
        TestRefusalCancellationAndOutputTransaction();
        TestPlanMemoryBoundariesAndUnchangedPlan();
        TestSourceRetentionAndWeakControlLifetime();
    }
    std::printf("%s selection-outline-raster.summary passed=%u failed=%u controls=%u\n", gFailed == 0 ? "PASS" : "FAIL",
                gPassed, gFailed, gPassed + gFailed);
    return static_cast<int>(gFailed);
}
