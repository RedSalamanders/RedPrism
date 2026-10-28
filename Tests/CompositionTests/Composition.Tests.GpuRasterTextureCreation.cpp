#include "Composition.Tests.GpuRasterTextureCreation.h"

#include "Common/Document.h"
#include "Common/RasterUploadLayout.h"
#include "RedPrism/GpuRasterTextureCreation.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <stop_token>
#include <thread>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
int gFailures = 0;

void Expect(bool condition, const char* label) noexcept
{
    std::printf("%s gpu-raster-create.%s\n", condition ? "PASS" : "FAIL", label);
    std::fflush(stdout);
    gFailures += condition ? 0 : 1;
}

void ExpectForCase(bool condition, const char* caseName, const char* assertion) noexcept
{
    std::array<char, 160> label{};
    const int written = std::snprintf(label.data(), label.size(), "%s.%s", caseName, assertion);
    Expect(written > 0 && static_cast<std::size_t>(written) < label.size() && condition,
           written > 0 && static_cast<std::size_t>(written) < label.size() ? label.data() : "case.label-overflow");
}

struct WarpDevice final
{
    wil::com_ptr_nothrow<ID3D11Device> device;
    wil::com_ptr_nothrow<ID3D11DeviceContext> context;

    HRESULT Create(unsigned int flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT) noexcept
    {
        constexpr D3D_FEATURE_LEVEL requested[] = {D3D_FEATURE_LEVEL_11_0};
        return D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, requested,
                                 static_cast<unsigned int>(std::size(requested)), D3D11_SDK_VERSION, device.put(),
                                 nullptr, context.put());
    }
};

struct WorkerReport final
{
    HRESULT result = E_PENDING;
    bool textureUnavailable = false;
    bool viewUnavailable = false;
    bool acknowledgementRefused = false;
    bool releaseRefused = false;
    bool signalled = false;
};

WorkerReport RunWorker(GpuRasterTextureCreation& creation, const RasterUploadLayout& layout, const char* caseName,
                       CancelToken cancellation = {})
{
    WorkerReport report;
    wil::unique_handle finished{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    ExpectForCase(finished != nullptr, caseName, "finished-event-created");
    if (!finished)
        return report;

    std::thread worker(
        [&]() noexcept
        {
            report.result = creation.CreateOnWorker(layout, cancellation);
            report.textureUnavailable = creation.TextureOwner() == nullptr;
            report.viewUnavailable = creation.ShaderViewOwner() == nullptr;
            report.acknowledgementRefused = !creation.AcknowledgeOwner();
            report.releaseRefused = !creation.ReleaseOwner();
            report.signalled = SetEvent(finished.get()) != FALSE;
        });
    const DWORD waitResult = WaitForSingleObject(finished.get(), 10000);
    worker.join();
    ExpectForCase(waitResult == WAIT_OBJECT_0, caseName, "worker-finished-within-bounded-wait");
    ExpectForCase(report.signalled, caseName, "worker-completion-event-signalled");
    return report;
}

RasterUploadLayout Layout(RasterFormat format, int width, int height, bool mipChain)
{
    constexpr std::array<std::uint8_t, 40> bgraPixels{};
    constexpr std::array<std::uint8_t, 10> grayPixels{};
    const std::uint8_t* bytes = format == RasterFormat::Gray8 ? grayPixels.data() : bgraPixels.data();
    const std::size_t byteCount =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * (format == RasterFormat::Gray8 ? 1U : 4U);
    const std::size_t capacity = format == RasterFormat::Gray8 ? grayPixels.size() : bgraPixels.size();
    if (byteCount > capacity)
        return {};
    const Raster source = Raster::Make(width, height, format, bytes, byteCount);
    return RasterUploadLayout::Make(source, 0, 0, width, height, 1, 4096, 4096, 4096, mipChain);
}

void CheckDescriptors(ID3D11Texture2D* texture, ID3D11ShaderResourceView* view, unsigned int width, unsigned int height,
                      DXGI_FORMAT format, unsigned int mipLevels, const char* textureLabel, const char* viewLabel,
                      const char* borrowLabel)
{
    Expect(texture != nullptr && view != nullptr, borrowLabel);
    if (!texture || !view)
        return;

    D3D11_TEXTURE2D_DESC actualTexture{};
    texture->GetDesc(&actualTexture);
    const bool textureMatches =
        actualTexture.Width == width && actualTexture.Height == height && actualTexture.MipLevels == mipLevels &&
        actualTexture.ArraySize == 1 && actualTexture.Format == format && actualTexture.SampleDesc.Count == 1 &&
        actualTexture.SampleDesc.Quality == 0 && actualTexture.Usage == D3D11_USAGE_DEFAULT &&
        actualTexture.BindFlags == (D3D11_BIND_SHADER_RESOURCE | (mipLevels > 1 ? D3D11_BIND_RENDER_TARGET : 0U)) &&
        actualTexture.CPUAccessFlags == 0 &&
        actualTexture.MiscFlags == (mipLevels > 1 ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0U);
    Expect(textureMatches, textureLabel);

    D3D11_SHADER_RESOURCE_VIEW_DESC actualView{};
    view->GetDesc(&actualView);
    const bool viewMatches = actualView.Format == format && actualView.ViewDimension == D3D11_SRV_DIMENSION_TEXTURE2D &&
                             actualView.Texture2D.MostDetailedMip == 0 && actualView.Texture2D.MipLevels == mipLevels;
    Expect(viewMatches, viewLabel);
}

void TestCaptureRefusals(WarpDevice& warp)
{
    GpuRasterTextureCreation empty;
    Expect(empty.CaptureOwner(nullptr, 31) == E_INVALIDARG, "capture.null-device-rejected");
    Expect(empty.CaptureOwner(warp.device.get(), 0) == E_INVALIDARG, "capture.zero-generation-rejected");
    Expect(empty.CaptureOwner(warp.device.get(), 31) == S_OK, "capture.valid-device-and-generation-accepted");
    Expect(empty.CaptureOwner(warp.device.get(), 32) == E_UNEXPECTED, "capture.second-capture-rejected");
    Expect(empty.DeviceGeneration() == 31, "capture.first-generation-retained");
    Expect(empty.ReleaseOwner(), "capture.refusal-fixture-retired");

    WarpDevice singleThreaded;
    const HRESULT createResult =
        singleThreaded.Create(D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_SINGLETHREADED);
    Expect(SUCCEEDED(createResult) && singleThreaded.device && singleThreaded.context,
           "capture.single-threaded-warp-device-created");
    if (SUCCEEDED(createResult) && singleThreaded.device)
    {
        GpuRasterTextureCreation refused;
        Expect(refused.CaptureOwner(singleThreaded.device.get(), 37) == E_INVALIDARG,
               "capture.single-threaded-device-refused");
        Expect(refused.DeviceGeneration() == 0 && refused.ReleaseOwner(),
               "capture.single-threaded-refusal-left-empty-state");
    }
}

void TestRoute(WarpDevice& warp, const RasterUploadLayout& layout, unsigned int expectedBytes,
               unsigned int expectedWidth, unsigned int expectedHeight, DXGI_FORMAT expectedFormat,
               unsigned int expectedMips, std::uint64_t generation, bool actualConcurrentCreates, const char* label,
               const char* layoutLabel, const char* captureLabel, const char* capabilityLabel, const char* textureLabel,
               const char* viewLabel, const char* borrowLabel)
{
    Expect(layout.IsValid(), layoutLabel);
    if (!layout.IsValid())
        return;
    ExpectForCase(layout.Width() == static_cast<int>(expectedWidth) &&
                      layout.Height() == static_cast<int>(expectedHeight) && layout.MipLevels() == expectedMips &&
                      layout.LogicalTextureBytes() == expectedBytes,
                  label, "literal-layout-and-logical-byte-expectations-match");

    GpuRasterTextureCreation creation;
    Expect(creation.CaptureOwner(warp.device.get(), generation) == S_OK, captureLabel);
    ExpectForCase(creation.DeviceGeneration() == generation, label, "device-generation-retained");
    if (creation.DeviceGeneration() != generation)
        return;
    ExpectForCase(creation.ThreadingQueryResult() == S_OK, label, "threading-feature-query-succeeded");
    Expect(creation.DriverConcurrentCreates() == actualConcurrentCreates, capabilityLabel);

    ExpectForCase(creation.TextureOwner() == nullptr && creation.ShaderViewOwner() == nullptr, label,
                  "resources-hidden-before-acknowledgement");
    ExpectForCase(!creation.AcknowledgeOwner(), label, "acknowledgement-before-worker-finish-refused");
    ExpectForCase(creation.CreateOnWorker(layout) == E_ACCESSDENIED, label, "owner-thread-create-refused");
    ExpectForCase(creation.ResourceCallsOwner() == 0, label, "owner-refusal-made-no-resource-calls");

    if (!actualConcurrentCreates)
    {
        const WorkerReport unsupported = RunWorker(creation, layout, label);
        const HRESULT actualUnsupported = HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
        ExpectForCase(unsupported.result == actualUnsupported, label, "actual-warp-concurrency-unsupported");
        ExpectForCase(unsupported.textureUnavailable && unsupported.viewUnavailable &&
                          unsupported.acknowledgementRefused && unsupported.releaseRefused,
                      label, "worker-owner-only-operations-refused");
        ExpectForCase(creation.AcknowledgeOwner(), label, "unsupported-worker-acknowledged-on-owner");
        ExpectForCase(creation.ResultOwner() == actualUnsupported && creation.ResourceCallsOwner() == 0, label,
                      "unsupported-result-and-zero-resource-calls");
        ExpectForCase(creation.TextureOwner() == nullptr && creation.ShaderViewOwner() == nullptr, label,
                      "unsupported-partial-resources-never-borrowable");
        ExpectForCase(!creation.AcknowledgeOwner(), label, "unsupported-acknowledgement-exactly-once");
        ExpectForCase(creation.ReleaseOwner(), label, "unsupported-owner-retirement");
        std::printf(
            "INFO gpu-raster-create.%s actual WARP DriverConcurrentCreates=FALSE; creation route returned 0x%08lx\n",
            label, static_cast<unsigned long>(actualUnsupported));
        return;
    }

    const WorkerReport worker = RunWorker(creation, layout, label);
    ExpectForCase(worker.result == S_OK, label, "worker-create-succeeded-on-advertised-route");
    ExpectForCase(worker.textureUnavailable && worker.viewUnavailable && worker.acknowledgementRefused &&
                      worker.releaseRefused,
                  label, "worker-cannot-borrow-acknowledge-or-retire");
    ExpectForCase(creation.TextureOwner() == nullptr && creation.ShaderViewOwner() == nullptr, label,
                  "finished-resources-hidden-until-owner-ack");
    ExpectForCase(creation.AcknowledgeOwner(), label, "owner-acknowledges-once-after-worker-join");
    ExpectForCase(!creation.AcknowledgeOwner(), label, "duplicate-acknowledgement-refused");
    ExpectForCase(creation.ResultOwner() == S_OK && creation.ResourceCallsOwner() == 2, label,
                  "actual-texture-and-view-call-counts");
    ID3D11Texture2D* texture = creation.TextureOwner();
    ID3D11ShaderResourceView* view = creation.ShaderViewOwner();
    CheckDescriptors(texture, view, expectedWidth, expectedHeight, expectedFormat, expectedMips, textureLabel,
                     viewLabel, borrowLabel);
    ExpectForCase(creation.ReleaseOwner(), label, "owner-releases-created-resource-set");
    ExpectForCase(creation.TextureOwner() == nullptr && creation.ShaderViewOwner() == nullptr, label,
                  "retired-objects-no-longer-borrowable");

    WorkerReport duplicate;
    wil::unique_handle duplicateFinished{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    ExpectForCase(duplicateFinished != nullptr, label, "duplicate-worker-event-created");
    if (duplicateFinished)
    {
        std::thread second(
            [&]() noexcept
            {
                duplicate.result = creation.CreateOnWorker(layout);
                duplicate.signalled = SetEvent(duplicateFinished.get()) != FALSE;
            });
        const DWORD duplicateWait = WaitForSingleObject(duplicateFinished.get(), 10000);
        second.join();
        ExpectForCase(duplicateWait == WAIT_OBJECT_0 && duplicate.signalled, label,
                      "duplicate-worker-completed-after-join");
        ExpectForCase(duplicate.result == E_UNEXPECTED, label, "duplicate-create-after-finish-refused");
    }
}

void TestEarlyWorkerRefusals(WarpDevice& warp, const RasterUploadLayout& valid)
{
    {
        GpuRasterTextureCreation creation;
        Expect(creation.CaptureOwner(warp.device.get(), 71) == S_OK, "early.cancelled-route-captured");
        std::stop_source source;
        source.request_stop();
        const WorkerReport worker = RunWorker(creation, valid, "early-pre-cancelled", CancelToken{source.get_token()});
        Expect(worker.result == HRESULT_FROM_WIN32(ERROR_CANCELLED), "early.pre-cancelled-worker-refused");
        Expect(creation.AcknowledgeOwner() && creation.ResultOwner() == HRESULT_FROM_WIN32(ERROR_CANCELLED) &&
                   creation.ResourceCallsOwner() == 0,
               "early.pre-cancelled-before-any-resource-call");
        Expect(creation.TextureOwner() == nullptr && creation.ShaderViewOwner() == nullptr,
               "early.cancelled-result-never-borrowable");
        Expect(creation.ReleaseOwner(), "early.cancelled-route-retired");
    }
    {
        GpuRasterTextureCreation creation;
        Expect(creation.CaptureOwner(warp.device.get(), 73) == S_OK, "early.invalid-layout-route-captured");
        const RasterUploadLayout invalid{};
        const WorkerReport worker = RunWorker(creation, invalid, "early-invalid-layout");
        Expect(worker.result == E_INVALIDARG, "early.invalid-layout-worker-refused");
        Expect(creation.AcknowledgeOwner() && creation.ResultOwner() == E_INVALIDARG &&
                   creation.ResourceCallsOwner() == 0,
               "early.invalid-layout-before-any-resource-call");
        Expect(creation.ReleaseOwner(), "early.invalid-layout-route-retired");
    }
    {
        GpuRasterTextureCreation creation;
        Expect(creation.CaptureOwner(warp.device.get(), 79) == S_OK, "early.restricted-route-captured");
        Expect(creation.RestrictConcurrencyForTest(), "early.forced-concurrency-refusal-installed");
        const WorkerReport worker = RunWorker(creation, valid, "early-forced-concurrency-refusal");
        const HRESULT unsupported = HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
        Expect(worker.result == unsupported, "early.forced-concurrency-refusal-result");
        Expect(creation.AcknowledgeOwner() && creation.ResultOwner() == unsupported &&
                   creation.ResourceCallsOwner() == 0,
               "early.forced-concurrency-refusal-before-resource-calls");
        Expect(creation.ReleaseOwner(), "early.restricted-route-retired");
    }
}

void TestFaults(WarpDevice& warp, const RasterUploadLayout& layout)
{
    for (const GpuRasterCreationFailure point :
         {GpuRasterCreationFailure::Texture, GpuRasterCreationFailure::ShaderView})
    {
        const char* caseName =
            point == GpuRasterCreationFailure::Texture ? "fault-texture-create" : "fault-shader-view";
        GpuRasterTextureCreation creation;
        ExpectForCase(creation.CaptureOwner(warp.device.get(), point == GpuRasterCreationFailure::Texture ? 83 : 89) ==
                          S_OK,
                      caseName, "route-captured");
        constexpr HRESULT injected = E_ACCESSDENIED;
        ExpectForCase(creation.SetFailureForTest(point, injected), caseName, "owner-installs-requested-fault");
        const WorkerReport worker = RunWorker(creation, layout, caseName);
        ExpectForCase(worker.result == injected, caseName, "injected-creation-error-returned");
        ExpectForCase(worker.textureUnavailable && worker.viewUnavailable && worker.acknowledgementRefused &&
                          worker.releaseRefused,
                      caseName, "worker-owner-operations-refused");
        ExpectForCase(creation.AcknowledgeOwner(), caseName, "owner-acknowledges-failed-creator");
        ExpectForCase(creation.ResultOwner() == injected, caseName, "original-injected-result-retained");
        const unsigned int expectedCalls = point == GpuRasterCreationFailure::Texture ? 0U : 1U;
        ExpectForCase(creation.ResourceCallsOwner() == expectedCalls, caseName, "resource-call-boundary-is-exact");
        ExpectForCase(creation.TextureOwner() == nullptr && creation.ShaderViewOwner() == nullptr, caseName,
                      "partial-objects-never-borrowable");
        ExpectForCase(creation.ReleaseOwner(), caseName, "owner-releases-any-partial-resource");
        ExpectForCase(creation.ResourceCallsOwner() == expectedCalls && creation.ResultOwner() == injected, caseName,
                      "retired-record-keeps-diagnostic-result");
    }
}

void TestDeviceReferenceSurvivesOwnerReset(WarpDevice& warp, WarpDevice& other)
{
    const RasterUploadLayout layout = Layout(RasterFormat::Bgra8888, 1, 4, false);
    GpuRasterTextureCreation creation;
    Expect(creation.CaptureOwner(warp.device.get(), 97) == S_OK, "retention.original-device-captured");
    const WorkerReport worker = RunWorker(creation, layout, "retention-original-device");
    Expect(worker.result == S_OK, "retention.texture-created-on-original-device");
    Expect(creation.AcknowledgeOwner(), "retention.creation-acknowledged");

    wil::com_ptr_nothrow<ID3D11Texture2D> retainedTexture;
    wil::com_ptr_nothrow<ID3D11ShaderResourceView> retainedView;
    retainedTexture = creation.TextureOwner();
    retainedView = creation.ShaderViewOwner();
    Expect(retainedTexture && retainedView, "retention.independent-com-owners-adopted-on-owner");
    if (!retainedTexture || !retainedView)
        return;
    const std::uint64_t generationBeforeReset = creation.DeviceGeneration();
    warp.context.reset();
    warp.device.reset();
    Expect(other.device && other.context, "retention.other-warp-device-remains-alive");
    Expect(creation.DeviceGeneration() == generationBeforeReset && generationBeforeReset == 97,
           "retention.generation-survives-original-device-owner-reset");
    Expect(creation.TextureOwner() != nullptr && creation.ShaderViewOwner() != nullptr,
           "retention.captured-device-keeps-owner-borrows-valid");
    Expect(creation.ReleaseOwner(), "retention.creation-owner-set-released");

    D3D11_TEXTURE2D_DESC independentTextureDesc{};
    retainedTexture->GetDesc(&independentTextureDesc);
    D3D11_SHADER_RESOURCE_VIEW_DESC independentViewDesc{};
    retainedView->GetDesc(&independentViewDesc);
    Expect(independentTextureDesc.Width == 1 && independentTextureDesc.Height == 4 &&
               independentTextureDesc.Format == DXGI_FORMAT_B8G8R8A8_UNORM && independentTextureDesc.MipLevels == 1,
           "retention.adopted-texture-survives-creation-release");
    Expect(independentViewDesc.Format == DXGI_FORMAT_B8G8R8A8_UNORM &&
               independentViewDesc.ViewDimension == D3D11_SRV_DIMENSION_TEXTURE2D &&
               independentViewDesc.Texture2D.MipLevels == 1,
           "retention.adopted-view-survives-creation-release");
    retainedView.reset();
    retainedTexture.reset();
}
} // namespace

int RunGpuRasterTextureCreationTests()
{
    gFailures = 0;
    WarpDevice warp;
    const HRESULT warpResult = warp.Create();
    Expect(SUCCEEDED(warpResult) && warp.device && warp.context, "fixture.actual-bgra-warp-device-created");
    if (FAILED(warpResult) || !warp.device || !warp.context)
        return gFailures;

    D3D11_FEATURE_DATA_THREADING actualThreading{};
    const HRESULT threadingResult =
        warp.device->CheckFeatureSupport(D3D11_FEATURE_THREADING, &actualThreading, sizeof(actualThreading));
    Expect(threadingResult == S_OK, "fixture.independent-threading-query-succeeded");
    if (threadingResult != S_OK)
        return gFailures;
    const bool concurrentCreates = actualThreading.DriverConcurrentCreates != FALSE;
    std::printf("INFO gpu-raster-create.fixture.actual-warp-threading-query=0x%08lx DriverConcurrentCreates=%s\n",
                static_cast<unsigned long>(threadingResult), concurrentCreates ? "TRUE" : "FALSE");

    TestCaptureRefusals(warp);
    const RasterUploadLayout bgraMipChain = Layout(RasterFormat::Bgra8888, 3, 2, true);
    const RasterUploadLayout grayMipChain = Layout(RasterFormat::Gray8, 3, 2, true);
    const RasterUploadLayout oneColumn = Layout(RasterFormat::Bgra8888, 1, 5, false);

    // Bind the real feature result to the fixture used by every route; the tests never force support to true.
    // The three descriptor routes each capture the exact WARP device and query its own threading feature.
    if (concurrentCreates)
    {
        TestRoute(warp, bgraMipChain, 28, 3, 2, DXGI_FORMAT_B8G8R8A8_UNORM, 2, 101, concurrentCreates, "bgra-3x2",
                  "layout.bgra-3x2-literal-28-bytes-two-mips", "capture.bgra-route-captures-device",
                  "capability.bgra-capture-matches-real-warp-query", "descriptor.bgra-3x2-texture2d-description",
                  "descriptor.bgra-3x2-shader-view-description", "descriptor.bgra-3x2-both-owner-borrows-present");
        TestRoute(warp, grayMipChain, 7, 3, 2, DXGI_FORMAT_R8_UNORM, 2, 103, concurrentCreates, "gray-3x2",
                  "layout.gray-3x2-literal-7-bytes-two-mips", "capture.gray-route-captures-device",
                  "capability.gray-capture-matches-real-warp-query", "descriptor.gray-3x2-texture2d-description",
                  "descriptor.gray-3x2-shader-view-description", "descriptor.gray-3x2-both-owner-borrows-present");
        TestRoute(warp, oneColumn, 20, 1, 5, DXGI_FORMAT_B8G8R8A8_UNORM, 1, 107, concurrentCreates, "bgra-1x5",
                  "layout.bgra-1x5-literal-20-bytes-one-mip", "capture.one-column-route-captures-device",
                  "capability.one-column-capture-matches-real-warp-query", "descriptor.bgra-1x5-texture2d-description",
                  "descriptor.bgra-1x5-shader-view-description", "descriptor.bgra-1x5-both-owner-borrows-present");
        TestEarlyWorkerRefusals(warp, bgraMipChain);
        TestFaults(warp, bgraMipChain);
        WarpDevice other;
        const HRESULT otherResult = other.Create();
        Expect(SUCCEEDED(otherResult) && other.device && other.context, "retention.second-warp-device-created");
        if (SUCCEEDED(otherResult) && other.device && other.context)
            TestDeviceReferenceSurvivesOwnerReset(warp, other);
    }
    else
    {
        TestRoute(warp, bgraMipChain, 28, 3, 2, DXGI_FORMAT_B8G8R8A8_UNORM, 2, 101, concurrentCreates,
                  "bgra-3x2-unsupported", "layout.unsupported-bgra-literal-28-bytes-two-mips",
                  "capture.unsupported-bgra-device-captured", "capability.unsupported-bgra-real-query-retained",
                  "descriptor.unavailable-bgra-texture-never-read", "descriptor.unavailable-bgra-view-never-read",
                  "descriptor.unavailable-bgra-borrows-null");
        TestRoute(warp, grayMipChain, 7, 3, 2, DXGI_FORMAT_R8_UNORM, 2, 103, concurrentCreates, "gray-3x2-unsupported",
                  "layout.unsupported-gray-literal-7-bytes-two-mips", "capture.unsupported-gray-device-captured",
                  "capability.unsupported-gray-real-query-retained", "descriptor.unavailable-gray-texture-never-read",
                  "descriptor.unavailable-gray-view-never-read", "descriptor.unavailable-gray-borrows-null");
        TestRoute(
            warp, oneColumn, 20, 1, 5, DXGI_FORMAT_B8G8R8A8_UNORM, 1, 107, concurrentCreates, "bgra-1x5-unsupported",
            "layout.unsupported-one-column-literal-20-bytes-one-mip", "capture.unsupported-one-column-device-captured",
            "capability.unsupported-one-column-real-query-retained",
            "descriptor.unavailable-one-column-texture-never-read", "descriptor.unavailable-one-column-view-never-read",
            "descriptor.unavailable-one-column-borrows-null");
        std::printf("INFO gpu-raster-create.capability positive descriptor routes unavailable because this WARP device "
                    "reports DriverConcurrentCreates=FALSE\n");
    }
    return gFailures;
}
