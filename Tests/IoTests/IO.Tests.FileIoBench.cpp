#include "IO.Tests.FileIoBench.h"

#include "Document.h"
#include "ImageCodecs.h"
#include "ProjectStore.h"

#include <windows.h>

#include <psapi.h>

#include <array>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <filesystem>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace
{
using Path = std::filesystem::path;
constexpr std::uint64_t kMaxFixturePixels = 100'000'000;
std::atomic<std::uint32_t> gSequence{0};

bool ParsePositive(const wchar_t* text, std::uint32_t& value) noexcept
{
    if (text == nullptr || *text == L'\0' || *text == L'-')
        return false;
    wchar_t* end = nullptr;
    errno = 0;
    const unsigned long parsed = std::wcstoul(text, &end, 10);
    if (errno != 0 || end == text || *end != L'\0' || parsed == 0 || parsed > std::numeric_limits<std::uint32_t>::max())
        return false;
    value = static_cast<std::uint32_t>(parsed);
    return true;
}

std::uint32_t Noise(std::uint32_t x, std::uint32_t y) noexcept
{
    std::uint32_t v = 0x6D2B79F5u ^ (x * 0x9E3779B9u) ^ (y * 0x85EBCA6Bu);
    v ^= v >> 15;
    v *= 0x2C1B3C6Du;
    v ^= v >> 12;
    v *= 0x297A2D39u;
    return v ^ (v >> 15);
}

struct Scratch final
{
    Path path;
    Path canonical;
    Path canonicalBase;
    ~Scratch()
    {
        std::error_code error;
        const Path current = std::filesystem::canonical(path, error);
        if (!error && current == canonical && current.parent_path() == canonicalBase)
            std::filesystem::remove_all(canonical, error);
    }
};

bool MakeScratch(Scratch& scratch)
{
    std::error_code error;
    Path base = std::filesystem::temp_directory_path(error) / L"RedPrism.FileIoBench";
    if (error)
        return false;
    std::filesystem::create_directories(base, error);
    if (error)
        return false;
    base = std::filesystem::canonical(base, error);
    if (error)
        return false;
    scratch.canonicalBase = base;
    for (unsigned attempt = 0; attempt != 64; ++attempt)
    {
        const std::wstring leaf = std::to_wstring(GetCurrentProcessId()) + L"." +
                                  std::to_wstring(gSequence.fetch_add(1, std::memory_order_relaxed));
        scratch.path = base / leaf;
        if (CreateDirectoryW(scratch.path.c_str(), nullptr))
        {
            scratch.canonical = std::filesystem::canonical(scratch.path, error);
            return !error && scratch.canonical.parent_path() == base;
        }
        if (GetLastError() != ERROR_ALREADY_EXISTS)
            return false;
    }
    return false;
}

bool ProcessMemory(std::uint64_t& privateBytes, std::uint64_t& peakWorkingSet) noexcept
{
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = static_cast<DWORD>(sizeof(counters));
    if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                              static_cast<DWORD>(sizeof(counters))))
        return false;
    privateBytes = static_cast<std::uint64_t>(counters.PrivateUsage);
    peakWorkingSet = static_cast<std::uint64_t>(counters.PeakWorkingSetSize);
    return true;
}

bool OutputSize(const Path& path, bool folder, std::uint64_t& bytes)
{
    std::error_code error;
    bytes = 0;
    if (!folder)
    {
        bytes = std::filesystem::file_size(path, error);
        return !error;
    }
    for (std::filesystem::recursive_directory_iterator it(path, error), end; !error && it != end; it.increment(error))
    {
        if (it->is_regular_file(error))
            bytes += it->file_size(error);
        if (error)
            return false;
    }
    return !error;
}

bool SamplesMatch(const Raster& raster, std::uint32_t width, std::uint32_t height) noexcept
{
    if (!raster.HasStorage() || raster.Width() != static_cast<int>(width) ||
        raster.Height() != static_cast<int>(height) || raster.Format() != RasterFormat::Bgra8888)
        return false;
    const std::array<std::pair<std::uint32_t, std::uint32_t>, 3> points{
        {{0, 0}, {width / 2, height / 2}, {width - 1, height - 1}}};
    const std::uint8_t* pixels = raster.Data();
    if (pixels == nullptr)
        return false;
    for (const auto [x, y] : points)
    {
        const std::uint32_t expected = Noise(x, y);
        const std::size_t index = (static_cast<std::size_t>(y) * width + x) * 4;
        if (pixels[index] != static_cast<std::uint8_t>(expected) ||
            pixels[index + 1] != static_cast<std::uint8_t>(expected >> 8) ||
            pixels[index + 2] != static_cast<std::uint8_t>(expected >> 16) || pixels[index + 3] != 255)
            return false;
    }
    return true;
}

void PrintFailure(const std::wstring& mode, std::uint32_t width, std::uint32_t height, std::uint32_t layers,
                  const char* status) noexcept
{
    std::printf("FILE BENCH mode=%ls width=%u height=%u layers=%u elapsedUs=0 privateBefore=0 privateAfter=0 "
                "peakWorkingSet=0 outputBytes=0 status=%s\n",
                mode.c_str(), width, height, layers, status);
}
} // namespace

int RunFileIoBench(int argc, wchar_t** argv)
{
    const std::wstring mode = argc > 2 && argv[2] != nullptr ? argv[2] : L"";
    std::uint32_t width = 0, height = 0, layers = 1;
    const bool argsOk = (argc == 5 || argc == 6) && ParsePositive(argv[3], width) && ParsePositive(argv[4], height) &&
                        (argc == 5 || ParsePositive(argv[5], layers)) && layers <= kMaxLayerCount &&
                        static_cast<std::uint64_t>(width) * height <= kMaxFixturePixels / layers &&
                        width <= static_cast<std::uint32_t>(kMaxCanvasSide) &&
                        height <= static_cast<std::uint32_t>(kMaxCanvasSide);
    const bool knownMode =
        mode == L"png-file" || mode == L"png-memory" || mode == L"zip-save" || mode == L"folder-save";
    if (!argsOk || !knownMode)
    {
        PrintFailure(knownMode ? mode : L"invalid", width, height, layers, "invalid-arguments");
        return 2;
    }

    PixelBuffer buffer = PixelBuffer::TryAllocate(static_cast<std::size_t>(width) * height * 4);
    if (!buffer)
    {
        PrintFailure(mode, width, height, layers, "fixture-allocation-failed");
        return 1;
    }
    for (std::uint32_t y = 0; y < height; ++y)
        for (std::uint32_t x = 0; x < width; ++x)
        {
            const std::uint32_t noise = Noise(x, y);
            const std::size_t i = (static_cast<std::size_t>(y) * width + x) * 4;
            buffer[i] = static_cast<std::uint8_t>(noise);
            buffer[i + 1] = static_cast<std::uint8_t>(noise >> 8);
            buffer[i + 2] = static_cast<std::uint8_t>(noise >> 16);
            buffer[i + 3] = 255;
        }
    const Raster image =
        Raster::Adopt(static_cast<int>(width), static_cast<int>(height), RasterFormat::Bgra8888, std::move(buffer));
    if (!image.HasStorage())
    {
        PrintFailure(mode, width, height, layers, "fixture-adoption-failed");
        return 1;
    }

    Scratch scratch;
    if (!MakeScratch(scratch))
    {
        PrintFailure(mode, width, height, layers, "scratch-create-failed");
        return 1;
    }
    const Path output = scratch.path / (mode == L"png-file"     ? L"fixture.png"
                                        : mode == L"png-memory" ? L"unused.png"
                                        : mode == L"zip-save"   ? L"fixture.prism"
                                                                : L"fixture-folder");
    CanvasDocument document;
    document.id = Guid::Generate();
    document.width = static_cast<int>(width);
    document.height = static_cast<int>(height);
    document.layers.reserve(layers);
    const Raster thumbnail = Raster::MakeThumbnail(image);
    for (std::uint32_t i = 0; i < layers; ++i)
    {
        ImageLayer layer = ImageLayer::Blank(L"Bench " + std::to_wstring(i),
                                             Vec2{static_cast<double>(width), static_cast<double>(height)});
        layer.asset = RasterAsset{image, thumbnail, layer.name};
        document.layers.push_back(std::move(layer));
    }

    std::vector<std::uint8_t> encoded;
    LARGE_INTEGER frequency{}, begin{}, end{};
    std::uint64_t privateBefore = 0, privateAfter = 0, peakWorkingSet = 0, outputBytes = 0;
    bool ok = QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0 &&
              ProcessMemory(privateBefore, peakWorkingSet) && QueryPerformanceCounter(&begin);
    if (ok)
    {
        if (mode == L"png-file")
            ok = EncodePngFile(image, 72, output.c_str()) == ImageCodecStatus::Ok;
        else if (mode == L"png-memory")
            ok = EncodePng(image, 72, encoded) == ImageCodecStatus::Ok;
        else
            ok = SaveProject(output.c_str(), document, std::nullopt, mode == L"zip-save") == ProjectIoStatus::Ok;
        ok = QueryPerformanceCounter(&end) && ok;
    }
    const std::uint64_t elapsedUs =
        ok ? static_cast<std::uint64_t>((end.QuadPart - begin.QuadPart) * 1'000'000 / frequency.QuadPart) : 0;
    ok = ProcessMemory(privateAfter, peakWorkingSet) && ok;
    const bool folder = mode == L"folder-save";
    outputBytes = mode == L"png-memory" ? encoded.size() : 0;
    if (ok && mode != L"png-memory")
        ok = OutputSize(output, folder, outputBytes);

    if (ok && mode == L"png-memory")
    {
        DecodedImage decoded;
        ok = DecodeProjectPng(encoded.data(), encoded.size(), false, static_cast<int>(kMaxLayerPixels), decoded) ==
                 ImageCodecStatus::Ok &&
             SamplesMatch(decoded.image, width, height);
    }
    else if (ok && (mode == L"png-file"))
    {
        DecodedImage decoded;
        ok = std::filesystem::is_regular_file(output) &&
             DecodeProjectPngFile(output.c_str(), false, static_cast<int>(kMaxLayerPixels), decoded) ==
                 ImageCodecStatus::Ok &&
             SamplesMatch(decoded.image, width, height);
    }
    else if (ok)
    {
        const ProjectLoadResult loaded = LoadProject(output.c_str());
        ok = (folder ? std::filesystem::is_directory(output) : std::filesystem::is_regular_file(output)) &&
             loaded.status == ProjectIoStatus::Ok && loaded.document.layers.size() == layers &&
             loaded.document.id == document.id && loaded.document.width == document.width &&
             loaded.document.height == document.height && loaded.document.resolution == document.resolution;
        for (std::size_t index = 0; ok && index < loaded.document.layers.size(); ++index)
            ok = loaded.document.layers[index].id == document.layers[index].id &&
                 loaded.document.layers[index].name == document.layers[index].name &&
                 loaded.document.layers[index].asset.has_value() &&
                 SamplesMatch(loaded.document.layers[index].asset->image, width, height);
    }
    // PeakWorkingSetSize is process-wide; it includes fixture creation and other setup allocations.
    std::printf("FILE BENCH mode=%ls width=%u height=%u layers=%u elapsedUs=%llu privateBefore=%llu privateAfter=%llu "
                "peakWorkingSet=%llu outputBytes=%llu status=%s\n",
                mode.c_str(), width, height, layers, static_cast<unsigned long long>(elapsedUs),
                static_cast<unsigned long long>(privateBefore), static_cast<unsigned long long>(privateAfter),
                static_cast<unsigned long long>(peakWorkingSet), static_cast<unsigned long long>(outputBytes),
                ok ? "ok" : "fail");
    return ok ? 0 : 1;
}
