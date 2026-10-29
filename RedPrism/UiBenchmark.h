#pragma once

#include "BenchmarkMachine.h"
#include "ContinuationRequests.h"
#include "Document.h"
#include "GpuWork.h"
#include "UiCancellationBenchmark.h"
#include "UiTiming.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

// Opt-in harness state only. Fixed per-operation sample storage lives on the heap, outside timed work.
// Application owns it until shutdown; every command is driven through the actual outer message/frame loop.
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626)
struct UiBenchmarkState final
{
    ~UiBenchmarkState(); // owned opt-in save/export fixture cleanup after Application joins workers
    enum class Phase
    {
        Preparing,
        Posted,
        AwaitingFrame,
        Done
    };
    struct Sample final
    {
        UiTimingStats timing;
        std::uint64_t elapsedUs = 0;
        std::uint64_t frames = 0;
        GpuReadbackStatistics readback;
        std::uint64_t sizeFinalPublications = 0;
    };
    std::unique_ptr<UiCancellationBenchmark> cancellation;
    CanvasDocument fixture;
    CanvasDocument imageImportBaseline;
    Guid activeLayer;
    BenchmarkMachineInfo machine;
    UiTimingStats timing;
    std::array<Sample, 10> samples;
    Phase phase = Phase::Preparing;
    std::size_t operation = 0;
    unsigned int trial = 0; // zero is the excluded warmup
    unsigned int runs = 10;
    bool smoke = false; // bounded correctness harness; may render while session display power is off
    BenchmarkDisplayContinuity displayContinuity;
    std::uintptr_t initialMonitorIdentity = 0;
    bool displayContinuityCaptured = false; // sealed once after setup, before any measured operation
    bool displayAuthorityKnown = false;     // monitor identity, device name, and rational refresh all resolved
    bool RejectPerformanceForDisplayChange() const noexcept
    {
        return !smoke && displayContinuityCaptured && displayContinuity.invalidated;
    }
    bool RejectPerformanceForUnknownDisplayAuthority() const noexcept
    {
        return !smoke && displayContinuityCaptured && !displayAuthorityKnown;
    }
    bool preparedDisplay = false; // experimental benchmark route; ordinary startup remains separate
    bool sizeOnly = false;        // explicit prepared Size final-publication benchmark roster
    bool samplerOnly = false;     // same production loop and fixture; skips unrelated operation setup
    std::uint64_t sizeFinalPublications = 0, previousSizeFinalPublications = 0;
    std::uint64_t previousPresents = 0;
    std::uint64_t previousReservedMemoryBytes = 0; // settled prepared caches; never an allowance for a sampler leak
    std::int64_t requestedAt = 0;
    std::uint64_t fixtureChecksum = 0;
    Guid previousRevision;
    Raster previousImage;
    Raster previousPreview;
    std::uint64_t previousPreviewSequence = 0;
    std::uint64_t gradientOpening = 0;
    std::uint64_t gradientSequence = 0;
    std::array<wchar_t, 32768> previousDocumentPath{};
    std::size_t previousDocumentPathUnits = 0;
    bool previousModified = false;
    GpuReadbackStatistics previousReadback;
    double previousZoom = 1;
    Vec2 previousPan{};
    bool complete = false;
    bool failed = false;
    bool savedBaselineReady = false;
    bool captureGradientApplyReceipt = false;
    std::uint64_t previousSessionSerial = 0;
    std::optional<Guid> previousActiveLayer;
    int previousUndoCount = 0;
    int previousErrorsShown = 0;
    int expectedImageWidth = 0;
    int expectedImageHeight = 0;
    std::wstring saveDirectory;
    std::wstring savePath;
    std::wstring pngExportPath;
    std::wstring jpegExportPath;
    bool ownsSaveDirectory = false;
    std::optional<ContinuationReceipt> saveReceipt;
    std::optional<ContinuationReceipt> openReceipt;
    std::optional<ContinuationReceipt> exportReceipt;
    std::optional<ContinuationReceipt> pngExportReceipt;
    std::optional<ContinuationReceipt> importReceipt;
    std::optional<ContinuationReceipt> gradientApplyReceipt;
    std::optional<ContinuationReceipt> samplerReceipt;
};
#pragma warning(pop)
