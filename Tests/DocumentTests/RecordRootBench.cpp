#include "RecordRootBench.h"

#include "Document.h"
#include "RecordRoot.h"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace
{
using Root = RecordRoot<ImageLayer>;
using Clock = std::chrono::steady_clock;

constexpr std::size_t kNameCharacters = 512;
constexpr std::array<std::size_t, 3> kSizes{1, 100, 10'000};
constexpr std::array<std::uint32_t, 3> kCaptureRootIterations{65'536, 65'536, 65'536};
constexpr std::array<std::uint32_t, 3> kCaptureVectorIterations{1'024, 256, 16};
constexpr std::array<std::uint32_t, 3> kMutationIterations{256, 64, 8};
constexpr double kMutatedOpacity = 0.375;

[[nodiscard]] std::wstring ExpectedName(std::size_t index)
{
    std::wstring name(kNameCharacters, L'a');
    for (std::size_t character = 0; character < name.size(); ++character)
    {
        const std::size_t letter = (index * 11 + character * 7) % 26;
        name[character] = static_cast<wchar_t>(L'a' + letter);
    }
    return name;
}

[[nodiscard]] Guid ExpectedGuid(std::size_t index) noexcept
{
    // The constant upper-byte pattern is (17 * byte + 3) mod 256. Keep it literal: MSVC's byte multiply
    // vectorizer emits first-party VEX/EVEX even at default /arch, outside the explicit guarded kernel tiers.
    Guid id{{0, 0, 37, 54, 71, 88, 105, 122, 139, 156, 173, 190, 207, 224, 241, 2}};
    id.bytes[0] = static_cast<std::uint8_t>(index & 0xffu);
    id.bytes[1] = static_cast<std::uint8_t>((index >> 8) & 0xffu);
    return id;
}

[[nodiscard]] ImageLayer MakeExpectedLayer(std::size_t index, const Raster& raster, bool mutated = false)
{
    ImageLayer layer;
    layer.id = ExpectedGuid(index);
    layer.asset = RasterAsset{raster, Raster{}, L""};
    layer.transform.origin = Vec2{static_cast<double>(index) + 0.25, static_cast<double>(index * 3) + 0.5};
    layer.transform.size = Vec2{static_cast<double>(index % 97 + 1), static_cast<double>(index % 53 + 2)};
    layer.transform.rotation = static_cast<double>(index % 31) * 0.125;
    layer.transform.flipX = (index % 2) != 0;
    layer.transform.flipY = (index % 3) == 0;
    layer.transform.sampling = LayerSampling::Nearest;
    layer.name = ExpectedName(index);
    layer.opacity = mutated ? kMutatedOpacity : 1.0;
    return layer;
}

[[nodiscard]] std::uint64_t Mix(std::uint64_t hash, std::uint64_t value) noexcept
{
    constexpr std::uint64_t kPrime = 1'099'511'628'211ull;
    return (hash ^ value) * kPrime;
}

// Keep sampling out of line so optimized benchmark builds must observe captured records and their child handles.
__declspec(noinline) [[nodiscard]] std::uint64_t ChecksumSamples(const std::vector<ImageLayer>& records) noexcept
{
    if (records.empty())
    {
        return 0;
    }
    const std::array<std::size_t, 3> indexes{0, records.size() / 2, records.size() - 1};
    std::uint64_t hash = 14'695'981'039'346'656'037ull;
    for (const std::size_t index : indexes)
    {
        const ImageLayer& layer = records[index];
        hash = Mix(hash, static_cast<std::uint64_t>(index));
        for (const std::uint8_t byte : layer.id.bytes)
        {
            hash = Mix(hash, byte);
        }
        hash = Mix(hash, std::bit_cast<std::uint64_t>(layer.transform.origin.x));
        hash = Mix(hash, std::bit_cast<std::uint64_t>(layer.transform.origin.y));
        hash = Mix(hash, std::bit_cast<std::uint64_t>(layer.transform.size.x));
        hash = Mix(hash, std::bit_cast<std::uint64_t>(layer.transform.size.y));
        hash = Mix(hash, std::bit_cast<std::uint64_t>(layer.transform.rotation));
        hash = Mix(hash, layer.transform.flipX ? 1u : 0u);
        hash = Mix(hash, layer.transform.flipY ? 1u : 0u);
        hash = Mix(hash, static_cast<std::uint64_t>(layer.transform.sampling));
        hash = Mix(hash, std::bit_cast<std::uint64_t>(layer.opacity));
        hash = Mix(hash, layer.name.size());
        for (const std::size_t character : {std::size_t{0}, kNameCharacters / 2, kNameCharacters - 1})
        {
            hash = Mix(hash, static_cast<std::uint64_t>(layer.name[character]));
        }
        const void* rasterIdentity = layer.asset.has_value() ? layer.asset.value().image.Identity() : nullptr;
        hash = Mix(hash, static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(rasterIdentity)));
    }
    return hash;
}

[[nodiscard]] std::uint64_t ExpectedChecksum(std::size_t count, bool mutated, const Raster& raster)
{
    std::uint64_t hash = 14'695'981'039'346'656'037ull;
    const std::array<std::size_t, 3> indexes{0, count / 2, count - 1};
    for (const std::size_t index : indexes)
    {
        const Guid id = ExpectedGuid(index);
        const Vec2 origin{static_cast<double>(index) + 0.25, static_cast<double>(index * 3) + 0.5};
        const Vec2 size{static_cast<double>(index % 97 + 1), static_cast<double>(index % 53 + 2)};
        const double rotation = static_cast<double>(index % 31) * 0.125;
        const std::wstring name = ExpectedName(index);
        hash = Mix(hash, static_cast<std::uint64_t>(index));
        for (const std::uint8_t byte : id.bytes)
        {
            hash = Mix(hash, byte);
        }
        hash = Mix(hash, std::bit_cast<std::uint64_t>(origin.x));
        hash = Mix(hash, std::bit_cast<std::uint64_t>(origin.y));
        hash = Mix(hash, std::bit_cast<std::uint64_t>(size.x));
        hash = Mix(hash, std::bit_cast<std::uint64_t>(size.y));
        hash = Mix(hash, std::bit_cast<std::uint64_t>(rotation));
        hash = Mix(hash, (index % 2) != 0 ? 1u : 0u);
        hash = Mix(hash, (index % 3) == 0 ? 1u : 0u);
        hash = Mix(hash, static_cast<std::uint64_t>(LayerSampling::Nearest));
        hash = Mix(hash, std::bit_cast<std::uint64_t>(mutated && index == count / 2 ? kMutatedOpacity : 1.0));
        hash = Mix(hash, name.size());
        for (const std::size_t character : {std::size_t{0}, kNameCharacters / 2, kNameCharacters - 1})
        {
            hash = Mix(hash, static_cast<std::uint64_t>(name[character]));
        }
        hash = Mix(hash, static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(raster.Identity())));
    }
    return hash;
}

[[nodiscard]] bool VerifyRecords(const std::vector<ImageLayer>& records, std::size_t count, const Raster& raster,
                                 bool mutated)
{
    if (records.size() != count)
    {
        return false;
    }
    const std::size_t changedIndex = count / 2;
    for (std::size_t index = 0; index < records.size(); ++index)
    {
        const ImageLayer& actual = records[index];
        if (actual != MakeExpectedLayer(index, raster, mutated && index == changedIndex) ||
            actual.name.size() != kNameCharacters || !actual.asset.has_value() ||
            actual.asset.value().image.Identity() != raster.Identity())
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool VerifyRoot(const Root& root, std::size_t count, const Raster& raster, bool mutated)
{
    const std::vector<ImageLayer>& records = root.Read();
    return VerifyRecords(records, count, raster, mutated);
}

void PrintLine(const char* operation, const char* path, std::size_t count, std::uint32_t round,
               std::uint32_t iterations, std::uint64_t elapsedNanoseconds, std::uint64_t checksum) noexcept
{
    std::printf("BENCH document-record-root op=%s path=%s records=%zu round=%u iterations=%u elapsed_ns=%llu "
                "checksum=%llu name_chars=%zu raster_handles=shared order=first-middle-last "
                "includes=sample-and-destruction\n",
                operation, path, count, round, iterations, static_cast<unsigned long long>(elapsedNanoseconds),
                static_cast<unsigned long long>(checksum), kNameCharacters);
}

[[nodiscard]] bool RunCaptureRound(const std::vector<ImageLayer>& sourceVector, const Root& sourceRoot,
                                   std::size_t count, std::uint32_t round, bool rootFirst, std::uint32_t rootIterations,
                                   std::uint32_t vectorIterations, std::uint64_t expectedChecksum)
{
    std::uint64_t rootChecksum = 0;
    std::uint64_t vectorChecksum = 0;
    const auto captureRoot = [&]()
    {
        const auto start = Clock::now();
        for (std::uint32_t iteration = 0; iteration < rootIterations; ++iteration)
        {
            Root capture = sourceRoot;
            rootChecksum += ChecksumSamples(capture.Read());
        }
        const std::uint64_t elapsed = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count());
        PrintLine("capture", "root", count, round, rootIterations, elapsed, rootChecksum);
    };
    const auto captureVector = [&]()
    {
        const auto start = Clock::now();
        for (std::uint32_t iteration = 0; iteration < vectorIterations; ++iteration)
        {
            std::vector<ImageLayer> capture = sourceVector;
            vectorChecksum += ChecksumSamples(capture);
        }
        const std::uint64_t elapsed = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count());
        PrintLine("capture", "vector", count, round, vectorIterations, elapsed, vectorChecksum);
    };
    if (rootFirst)
    {
        captureRoot();
        captureVector();
    }
    else
    {
        captureVector();
        captureRoot();
    }
    return rootChecksum == expectedChecksum * rootIterations && vectorChecksum == expectedChecksum * vectorIterations;
}

[[nodiscard]] bool RunMutationRound(const std::vector<ImageLayer>& sourceVector, const Root& sourceRoot,
                                    const ImageLayer& changedLayer, std::size_t count, std::uint32_t round,
                                    bool rootFirst, std::uint32_t iterations, std::uint64_t expectedChecksum)
{
    std::uint64_t rootChecksum = 0;
    std::uint64_t vectorChecksum = 0;
    const std::size_t changedIndex = count / 2;
    const auto mutateRoot = [&]()
    {
        const auto start = Clock::now();
        for (std::uint32_t iteration = 0; iteration < iterations; ++iteration)
        {
            std::optional<Root::Builder> builder = Root::Builder::Clone(sourceRoot, count);
            if (!builder.has_value())
            {
                return false;
            }
            ImageLayer replacement = changedLayer;
            if (!builder.value().Replace(changedIndex, std::move(replacement)))
            {
                return false;
            }
            std::optional<Root> result = builder.value().Seal();
            if (!result.has_value())
            {
                return false;
            }
            rootChecksum += ChecksumSamples(result.value().Read());
        }
        const std::uint64_t elapsed = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count());
        PrintLine("clone-replace-seal", "root", count, round, iterations, elapsed, rootChecksum);
        return true;
    };
    const auto mutateVector = [&]()
    {
        const auto start = Clock::now();
        for (std::uint32_t iteration = 0; iteration < iterations; ++iteration)
        {
            std::vector<ImageLayer> result = sourceVector;
            ImageLayer replacement = changedLayer;
            result[changedIndex] = std::move(replacement);
            vectorChecksum += ChecksumSamples(result);
        }
        const std::uint64_t elapsed = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count());
        PrintLine("copy-replace", "vector", count, round, iterations, elapsed, vectorChecksum);
        return true;
    };
    bool rootOk = true;
    bool vectorOk = true;
    if (rootFirst)
    {
        rootOk = mutateRoot();
        vectorOk = rootOk && mutateVector();
    }
    else
    {
        vectorOk = mutateVector();
        rootOk = vectorOk && mutateRoot();
    }
    return rootOk && vectorOk && rootChecksum == expectedChecksum * iterations &&
           vectorChecksum == expectedChecksum * iterations;
}

[[nodiscard]] bool RunSize(std::size_t count, std::size_t sizeIndex, const Raster& raster)
{
    std::vector<ImageLayer> sourceVector;
    sourceVector.reserve(count);
    for (std::size_t index = 0; index < count; ++index)
    {
        sourceVector.push_back(MakeExpectedLayer(index, raster));
    }
    {
        CanvasDocument validation;
        validation.width = kMaxCanvasSide;
        validation.height = kMaxCanvasSide;
        validation.layers = sourceVector;
        if (ValidateDocument(validation) != DocumentStatus::Ok)
        {
            std::printf("BENCH document-record-root status=FAIL phase=document-fixture-validation records=%zu\n",
                        count);
            return false;
        }
    }
    Root::Builder rootBuilder(count);
    for (const ImageLayer& layer : sourceVector)
    {
        if (!rootBuilder.Append(layer))
        {
            std::printf("BENCH document-record-root status=FAIL phase=source-seal records=%zu\n", count);
            return false;
        }
    }
    std::optional<Root> sourceRoot = rootBuilder.Seal();
    if (!sourceRoot.has_value() || !VerifyRecords(sourceVector, count, raster, false) ||
        !VerifyRoot(sourceRoot.value(), count, raster, false))
    {
        std::printf("BENCH document-record-root status=FAIL phase=source-validation records=%zu\n", count);
        return false;
    }

    const std::size_t changedIndex = count / 2;
    const ImageLayer changedLayer = MakeExpectedLayer(changedIndex, raster, true);
    const std::uint64_t captureChecksum = ExpectedChecksum(count, false, raster);
    const std::uint64_t mutationChecksum = ExpectedChecksum(count, true, raster);
    for (std::uint32_t round = 0; round < 5; ++round)
    {
        const bool rootFirst = (round % 2) == 0;
        if (!RunCaptureRound(sourceVector, sourceRoot.value(), count, round + 1, rootFirst,
                             kCaptureRootIterations[sizeIndex], kCaptureVectorIterations[sizeIndex], captureChecksum))
        {
            std::printf("BENCH document-record-root status=FAIL phase=capture-check records=%zu round=%u\n", count,
                        round + 1);
            return false;
        }
        if (!RunMutationRound(sourceVector, sourceRoot.value(), changedLayer, count, round + 1, rootFirst,
                              kMutationIterations[sizeIndex], mutationChecksum))
        {
            std::printf("BENCH document-record-root status=FAIL phase=mutation-check records=%zu round=%u\n", count,
                        round + 1);
            return false;
        }
    }

    std::vector<ImageLayer> expectedMutation = sourceVector;
    expectedMutation[changedIndex] = changedLayer;
    std::optional<Root::Builder> finalBuilder = Root::Builder::Clone(sourceRoot.value(), count);
    if (!finalBuilder.has_value())
    {
        std::printf("BENCH document-record-root status=FAIL phase=final-clone records=%zu\n", count);
        return false;
    }
    ImageLayer finalReplacement = changedLayer;
    if (!finalBuilder.value().Replace(changedIndex, std::move(finalReplacement)))
    {
        std::printf("BENCH document-record-root status=FAIL phase=final-replace records=%zu\n", count);
        return false;
    }
    std::optional<Root> finalRoot = finalBuilder.value().Seal();
    if (!finalRoot.has_value() || !VerifyRecords(sourceVector, count, raster, false) ||
        !VerifyRoot(sourceRoot.value(), count, raster, false) ||
        !VerifyRecords(expectedMutation, count, raster, true) || !VerifyRoot(finalRoot.value(), count, raster, true) ||
        !std::equal(expectedMutation.begin(), expectedMutation.end(), finalRoot.value().begin()))
    {
        std::printf("BENCH document-record-root status=FAIL phase=final-validation records=%zu\n", count);
        return false;
    }
    return true;
}
} // namespace

int RunRecordRootBench()
{
    std::printf("BENCH document-record-root warning=primitive-records-only; not-migrated-CanvasDocument; "
                "not-G1-RAW-Photoshop\n");
    constexpr std::array<std::uint8_t, 4> pixel{0x31, 0x72, 0xb4, 0xff};
    const Raster raster = Raster::Make(1, 1, RasterFormat::Bgra8888, pixel.data(), pixel.size());
    if (!raster.HasStorage())
    {
        std::printf("BENCH document-record-root status=FAIL phase=raster-creation\n");
        return 1;
    }
    for (std::size_t sizeIndex = 0; sizeIndex < kSizes.size(); ++sizeIndex)
    {
        if (!RunSize(kSizes[sizeIndex], sizeIndex, raster))
        {
            return 1;
        }
    }
    std::printf("BENCH document-record-root status=complete qualification=none\n");
    return 0;
}
