#include "Composition.Tests.UtilityAdmission.h"

#include "UtilityReadbackAdmission.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace RedPrism::CompositionTests
{
namespace
{
unsigned int gFailures = 0;
unsigned int gChecks = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s utility-admission.%s\n", condition ? "PASS" : "FAIL", name);
    ++gChecks;
    gFailures += condition ? 0u : 1u;
}

std::optional<CanvasDocument> MakeDocument(bool sparse)
{
    constexpr std::array<std::uint8_t, 16> kDensePixels{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    constexpr std::array<std::uint8_t, 4> kTilePixels{21, 22, 23, 24};
    const Raster dense = Raster::Make(2, 2, RasterFormat::Bgra8888, kDensePixels.data(), kDensePixels.size());
    if (!dense.HasStorage())
        return std::nullopt;

    Raster image = dense;
    if (sparse)
    {
        const Raster tile = Raster::Make(1, 1, RasterFormat::Bgra8888, kTilePixels.data(), kTilePixels.size());
        if (!tile.HasStorage())
            return std::nullopt;
        image = Raster::MakeSparse(2, 2, RasterFormat::Bgra8888, dense, 0, 0, {RasterTile{1, 1, tile}}, 0);
        if (!image.HasStorage() || !image.IsSparse())
            return std::nullopt;
    }

    CanvasDocument document;
    document.width = 2;
    document.height = 2;
    ImageLayer layer = ImageLayer::Blank(sparse ? L"Utility sparse" : L"Utility dense", {2, 2});
    layer.transform.sampling = LayerSampling::Nearest;
    layer.asset = RasterAsset{std::move(image), {}, sparse ? L"Utility sparse" : L"Utility dense"};

    RecordRoot<ImageLayer>::Builder builder(1);
    if (!builder.CanSeal() || !builder.Append(std::move(layer)))
        return std::nullopt;
    auto root = builder.Seal();
    if (!root)
        return std::nullopt;
    document.layers = std::move(root.value());
    return document;
}

bool SameNonCpuEnvelope(const UtilityReadbackAdmission::Envelope& left,
                        const UtilityReadbackAdmission::Envelope& right) noexcept
{
    return left.sourceBytes == right.sourceBytes && left.rasterNodes == right.rasterNodes &&
           left.gpuBytes == right.gpuBytes && left.logicalGpuBytes == right.logicalGpuBytes &&
           left.tileSide == right.tileSide;
}

void CheckSmallDocument(bool sparse)
{
    auto source = MakeDocument(sparse);
    Expect(source.has_value(), sparse ? "sparse.fixture" : "dense.fixture");
    if (!source)
        return;

    const auto expectedOwnership = source->layers.OwnershipBounds();
    const auto base = UtilityReadbackAdmission::Estimate(source.value(), 2, 0, false);
    Expect(base.has_value(), sparse ? "sparse.admitted" : "dense.admitted");
    if (!base)
        return;

    // Source ownership is independently published by the document/root producer caches.
    Expect(base->sourceBytes == source->RetirementMemoryBytes(), sparse ? "sparse.source-bytes" : "dense.source-bytes");
    Expect(expectedOwnership.has_value() && base->rasterNodes == expectedOwnership->rasterNodes,
           sparse ? "sparse.raster-nodes" : "dense.raster-nodes");
    Expect(base->tileSide == 2, sparse ? "sparse.tile-side" : "dense.tile-side");

    Expect(base->rasterNodes == (sparse ? 3u : 1u),
           sparse ? "sparse.literal-three-raster-owners" : "dense.literal-one-raster-owner");

    constexpr std::uint64_t kAdditionalCpuBytes = 12345;
    const auto withAdditional = UtilityReadbackAdmission::Estimate(source.value(), 2, kAdditionalCpuBytes, false);
    Expect(withAdditional.has_value(), sparse ? "sparse.additional-admitted" : "dense.additional-admitted");
    if (withAdditional)
    {
        Expect(withAdditional->cpuBytes - base->cpuBytes == 12345,
               sparse ? "sparse.additional-is-literal" : "dense.additional-is-literal");
        Expect(SameNonCpuEnvelope(base.value(), withAdditional.value()),
               sparse ? "sparse.additional-is-cpu-only" : "dense.additional-is-cpu-only");
    }

    const auto software = UtilityReadbackAdmission::Estimate(source.value(), 2, 0, true);
    Expect(software.has_value(), sparse ? "sparse.software-admitted" : "dense.software-admitted");
    if (software)
    {
        Expect(software->cpuBytes - base->cpuBytes == base->gpuBytes,
               sparse ? "sparse.software-cpu-adds-gpu-envelope" : "dense.software-cpu-adds-gpu-envelope");
        Expect(SameNonCpuEnvelope(base.value(), software.value()),
               sparse ? "sparse.software-same-gpu-envelope" : "dense.software-same-gpu-envelope");
    }
}

void CheckLiteralArithmetic()
{
    // Independently enumerated policy totals, excluding ABI-dependent document metadata.
    // Source bounds below are arithmetic fixtures, not fake document or GPU admission.
    struct Case final
    {
        UtilityReadbackAdmission::Descriptor descriptor;
        std::uint64_t cpu;
        std::uint64_t gpu;
        std::uint64_t logical;
        const char* name;
    };
    constexpr std::array<Case, 4> cases{
        {{{2, 2, 1, 4096, 1, 2, 0, false}, 11547152, 135468632, 595544, "literal.dense-envelope"},
         {{2, 2, 1, 8192, 3, 2, 0, false}, 11556368, 135747160, 611928, "literal.sparse-envelope"},
         {{2, 2, 1, 4096, 1, 2, 0, true}, 147015784, 135468632, 595544, "literal.software-envelope"},
         {{2, 2, 0, 4096, 0, 2, 0, false}, 11542544, 134496320, 16448, "literal.empty-layer-envelope"}}};
    for (const auto& test : cases)
    {
        const auto result = UtilityReadbackAdmission::Estimate(test.descriptor);
        Expect(result && result->cpuBytes == test.cpu && result->gpuBytes == test.gpu &&
                   result->logicalGpuBytes == test.logical && result->sourceBytes == test.descriptor.sourceBytes &&
                   result->rasterNodes == test.descriptor.rasterNodes && result->tileSide == 2,
               test.name);
    }
    auto invalid = cases[0].descriptor;
    invalid.sourceBytes = 0;
    Expect(!UtilityReadbackAdmission::Estimate(invalid), "invalid.unknown-source-bound");
    invalid = cases[0].descriptor;
    invalid.sourceBytes = (std::numeric_limits<std::uint64_t>::max)();
    Expect(!UtilityReadbackAdmission::Estimate(invalid), "invalid.source-multiplication-overflow");
    invalid = cases[0].descriptor;
    invalid.rasterNodes = (std::numeric_limits<std::uint64_t>::max)();
    Expect(!UtilityReadbackAdmission::Estimate(invalid), "invalid.node-multiplication-overflow");
    invalid = cases[0].descriptor;
    invalid.layerCount = kMaxLayerCount + 1ull;
    Expect(!UtilityReadbackAdmission::Estimate(invalid), "invalid.layer-count-over-limit");
    invalid = cases[0].descriptor;
    invalid.additionalCpuBytes = (std::numeric_limits<std::uint64_t>::max)() - 11547152;
    const auto exact = UtilityReadbackAdmission::Estimate(invalid);
    Expect(exact && exact->cpuBytes == (std::numeric_limits<std::uint64_t>::max)(), "literal.exact-uint64-limit");
    ++invalid.additionalCpuBytes;
    Expect(!UtilityReadbackAdmission::Estimate(invalid), "invalid.one-byte-cpu-overflow");
    --invalid.additionalCpuBytes;
    invalid.software = true;
    Expect(!UtilityReadbackAdmission::Estimate(invalid), "invalid.software-domain-overflow");
}

void CheckRejectedInputs()
{
    auto source = MakeDocument(false);
    Expect(source.has_value(), "invalid.fixture");
    if (!source)
        return;

    CanvasDocument invalid = source.value();
    invalid.width = 0;
    Expect(!UtilityReadbackAdmission::Estimate(invalid, 2, 0, false), "invalid.zero-width");
    invalid = source.value();
    invalid.height = kMaxCanvasSide + 1;
    Expect(!UtilityReadbackAdmission::Estimate(invalid, 2, 0, false), "invalid.height-over-limit");

    Expect(!UtilityReadbackAdmission::Estimate(source.value(), 0, 0, false), "invalid.zero-tile-side");
    Expect(!UtilityReadbackAdmission::Estimate(source.value(), 2049, 0, false), "invalid.tile-side-over-limit");
    Expect(!UtilityReadbackAdmission::Estimate(source.value(), 2, (std::numeric_limits<std::uint64_t>::max)(), false),
           "invalid.additional-cpu-overflow");
}
} // namespace

int RunUtilityAdmissionTests()
{
    gFailures = gChecks = 0;
    CheckLiteralArithmetic();
    CheckSmallDocument(false);
    CheckSmallDocument(true);
    CheckRejectedInputs();
    std::printf("SUMMARY utility-admission checks=%u failed=%u\n", gChecks, gFailures);
    return static_cast<int>(gFailures);
}
} // namespace RedPrism::CompositionTests
