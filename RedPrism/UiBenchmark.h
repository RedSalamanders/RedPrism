#pragma once

#include "BenchmarkMachine.h"
#include "ContinuationRequests.h"
#include "Document.h"
#include "GpuWork.h"
#include "UiTiming.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

// Opt-in harness state only. Fixed per-operation sample storage lives on the heap, outside timed work.
// Application owns it until shutdown; every command is driven through the actual outer message/frame loop.
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
    };
    CanvasDocument fixture;
    Guid activeLayer;
    BenchmarkMachineInfo machine;
    UiTimingStats timing;
    std::array<Sample, 10> samples;
    Phase phase = Phase::Preparing;
    std::size_t operation = 0;
    unsigned int trial = 0; // zero is the excluded warmup
    unsigned int runs = 10;
    bool smoke = false; // bounded correctness harness; may render while session display power is off
    std::uint64_t previousPresents = 0;
    std::int64_t requestedAt = 0;
    std::uint64_t fixtureChecksum = 0;
    Guid previousRevision;
    Raster previousImage;
    Raster previousPreview;
    std::uint64_t previousPreviewSequence = 0;
    std::array<wchar_t, 32768> previousDocumentPath{};
    std::size_t previousDocumentPathUnits = 0;
    bool previousModified = false;
    GpuReadbackStatistics previousReadback;
    double previousZoom = 1;
    Vec2 previousPan{};
    bool complete = false;
    bool failed = false;
    bool savedBaselineReady = false;
    std::uint64_t previousSessionSerial = 0;
    std::wstring saveDirectory;
    std::wstring savePath;
    std::wstring pngExportPath;
    std::wstring jpegExportPath;
    bool ownsSaveDirectory = false;
    std::optional<ContinuationReceipt> saveReceipt;
    std::optional<ContinuationReceipt> openReceipt;
    std::optional<ContinuationReceipt> exportReceipt;
};
