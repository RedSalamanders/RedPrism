#include "GpuUploadAdmissionTests.h"

#include "GpuUploadWork.h"
#include "OwnerWorkMultiplexer.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <limits>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    std::printf("%s gpu-upload-admission.%s\n", condition ? "PASS" : "FAIL", name);
    gFailures += condition ? 0 : 1;
}

bool StableMemory(void*, MemorySnapshot& snapshot) noexcept
{
    constexpr std::uint64_t available = 8ULL * 1024 * 1024 * 1024;
    snapshot = {available, available};
    return true;
}

struct Device final
{
    wil::com_ptr_nothrow<ID3D11Device> device;
    wil::com_ptr_nothrow<ID3D11DeviceContext> context;

    HRESULT Create() noexcept
    {
        const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
        return D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, &level, 1,
                                 D3D11_SDK_VERSION, device.put(), nullptr, context.put());
    }
};

wil::com_ptr_nothrow<ID3D11Texture2D> Texture(ID3D11Device* device, unsigned int width, unsigned int height,
                                              DXGI_FORMAT format = DXGI_FORMAT_B8G8R8A8_UNORM,
                                              D3D11_USAGE usage = D3D11_USAGE_DEFAULT, unsigned int mipLevels = 1,
                                              unsigned int arraySize = 1, unsigned int sampleCount = 1)
{
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = mipLevels;
    desc.ArraySize = arraySize;
    desc.Format = format;
    desc.SampleDesc.Count = sampleCount;
    desc.Usage = usage;
    if (sampleCount > 1)
        desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    if (usage == D3D11_USAGE_STAGING)
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
    wil::com_ptr_nothrow<ID3D11Texture2D> texture;
    if (device)
        (void)device->CreateTexture2D(&desc, nullptr, texture.put());
    return texture;
}

class Receiver final : public GpuUploadReceiver
{
  public:
    unsigned int rowsCalls = 0;
    unsigned int finishedCalls = 0;
    JobTicket rowsTicket{};
    JobTicket finishedTicket{};
    GpuUploadRows rows{};
    HRESULT result = E_PENDING;

    void WritableRowsReady(JobTicket ticket, GpuUploadRows readyRows) noexcept override
    {
        ++rowsCalls;
        rowsTicket = ticket;
        rows = readyRows;
    }

    void UploadFinished(JobTicket ticket, HRESULT finishedResult) noexcept override
    {
        ++finishedCalls;
        finishedTicket = ticket;
        result = finishedResult;
    }
};

constexpr JobTicket kTicket{7, 11};
constexpr std::size_t kExpectedUploadCapacity = 8;

void TestBeforeInitialization(ID3D11Texture2D* texture)
{
    Receiver receiver;
    GpuUploadQueue queue;
    Expect(queue.Enqueue(texture, 0, 0, 0, 1, 1, kTicket, receiver) == HRESULT_FROM_WIN32(ERROR_INVALID_STATE),
           "before-init-refused");
    Expect(receiver.rowsCalls == 0 && receiver.finishedCalls == 0, "before-init-no-callback");
}

void TestAdmission(Device& device, Device& otherDevice)
{
    JobScheduler scheduler;
    OwnerWorkMultiplexer routes;
    Receiver receiver;
    GpuUploadQueue queue;
    GpuUploadQueue capacityQueue;
    const HRESULT initialized = scheduler.Initialize(1, {}, {StableMemory, nullptr}) == S_OK &&
                                        scheduler.RegisterOwnerWork(routes) &&
                                        SUCCEEDED(queue.Initialize(scheduler, &routes)) &&
                                        SUCCEEDED(capacityQueue.Initialize(scheduler, &routes))
                                    ? S_OK
                                    : E_FAIL;
    Expect(SUCCEEDED(initialized), "initialize-multiplexed-queue");
    if (FAILED(initialized))
    {
        queue.StopNotifications();
        capacityQueue.StopNotifications();
        scheduler.Close();
        queue.CloseAfterWorkers();
        capacityQueue.CloseAfterWorkers();
        return;
    }
    auto basic = Texture(device.device.get(), 32, 16);
    Expect(basic != nullptr, "basic-warp-texture-created");
    if (!basic)
    {
        queue.StopNotifications();
        capacityQueue.StopNotifications();
        scheduler.Close();
        queue.CloseAfterWorkers();
        capacityQueue.CloseAfterWorkers();
        return;
    }

    Expect(queue.Enqueue(basic.get(), 0, 0, 0, 1, 1, kTicket, receiver) == HRESULT_FROM_WIN32(ERROR_INVALID_STATE),
           "before-bind-refused");
    Expect(SUCCEEDED(queue.Bind(device.device.get(), device.context.get())), "bind-warp-device");
    Expect(SUCCEEDED(capacityQueue.Bind(device.device.get(), device.context.get())), "bind-capacity-warp-device");

    Expect(queue.Enqueue(nullptr, 0, 0, 0, 1, 1, kTicket, receiver) == E_INVALIDARG, "null-destination-refused");
    Expect(queue.Enqueue(basic.get(), 0, 0, 0, 0, 1, kTicket, receiver) == E_INVALIDARG, "zero-width-refused");
    Expect(queue.Enqueue(basic.get(), 0, 0, 0, 1, 0, kTicket, receiver) == E_INVALIDARG, "zero-height-refused");
    Expect(queue.Enqueue(basic.get(), 0, 0, 0, 1, 1, {128, 1}, receiver) == E_INVALIDARG,
           "ticket-slot-out-of-range-refused");
    Expect(queue.Enqueue(basic.get(), 0, 0, 0, 1, 1, {}, receiver) == E_INVALIDARG, "default-ticket-refused");
    Expect(queue.Enqueue(basic.get(), 0, 0, 0, 1, 1, {2, 0}, receiver) == E_INVALIDARG,
           "ticket-zero-generation-refused");
    Expect(queue.Enqueue(basic.get(), 1, 0, 0, 1, 1, kTicket, receiver) == E_INVALIDARG,
           "subresource-out-of-range-refused");
    Expect(queue.Enqueue(basic.get(), 0, 32, 0, 1, 1, kTicket, receiver) == E_INVALIDARG, "x-origin-at-width-refused");
    Expect(queue.Enqueue(basic.get(), 0, 31, 0, 2, 1, kTicket, receiver) == E_INVALIDARG, "x-crossing-edge-refused");
    Expect(queue.Enqueue(basic.get(), 0, 0, 16, 1, 1, kTicket, receiver) == E_INVALIDARG, "y-origin-at-height-refused");
    Expect(queue.Enqueue(basic.get(), 0, 0, 15, 1, 2, kTicket, receiver) == E_INVALIDARG, "y-crossing-edge-refused");

    auto foreign = Texture(otherDevice.device.get(), 8, 8);
    Expect(foreign != nullptr, "foreign-device-texture-created");
    Expect(foreign && queue.Enqueue(foreign.get(), 0, 0, 0, 1, 1, kTicket, receiver) == E_INVALIDARG,
           "wrong-device-refused");

    auto unsupportedFormat = Texture(device.device.get(), 8, 8, DXGI_FORMAT_R16G16B16A16_FLOAT);
    Expect(unsupportedFormat != nullptr, "unsupported-format-texture-created");
    Expect(unsupportedFormat &&
               queue.Enqueue(unsupportedFormat.get(), 0, 0, 0, 1, 1, kTicket, receiver) == E_INVALIDARG,
           "incompatible-format-refused");

    auto staging = Texture(device.device.get(), 8, 8, DXGI_FORMAT_B8G8R8A8_UNORM, D3D11_USAGE_STAGING);
    Expect(staging != nullptr, "staging-texture-created");
    Expect(staging && queue.Enqueue(staging.get(), 0, 0, 0, 1, 1, kTicket, receiver) == E_INVALIDARG,
           "incompatible-usage-refused");

    auto multisampled = Texture(device.device.get(), 8, 8, DXGI_FORMAT_B8G8R8A8_UNORM, D3D11_USAGE_DEFAULT, 1, 1, 2);
    Expect(multisampled != nullptr, "multisampled-texture-created");
    Expect(multisampled && queue.Enqueue(multisampled.get(), 0, 0, 0, 1, 1, kTicket, receiver) == E_INVALIDARG,
           "multisampled-texture-refused");

    auto mipArray = Texture(device.device.get(), 16, 8, DXGI_FORMAT_B8G8R8A8_UNORM, D3D11_USAGE_DEFAULT, 4, 2);
    Expect(mipArray != nullptr, "mip-array-texture-created");
    Expect(mipArray && queue.Enqueue(mipArray.get(), 5, 6, 2, 2, 2, kTicket, receiver) == S_OK,
           "array-mip-exact-edge-admitted");
    Expect(mipArray && queue.Enqueue(mipArray.get(), 8, 0, 0, 1, 1, kTicket, receiver) == E_INVALIDARG,
           "array-mip-subresource-count-refused");
    Expect(queue.Enqueue(basic.get(), 0, 31, 15, 1, 1, kTicket, receiver) == S_OK, "exact-image-edge-admitted");

    auto exactChunk = Texture(device.device.get(), 4096, 64);
    Expect(exactChunk != nullptr, "exact-chunk-texture-created");
    Expect(exactChunk && queue.Enqueue(exactChunk.get(), 0, 0, 0, 4096, 64, kTicket, receiver) == S_OK,
           "exact-one-mib-chunk-admitted");
    auto overChunk = Texture(device.device.get(), 4096, 65);
    Expect(overChunk != nullptr, "over-chunk-texture-created");
    Expect(overChunk && queue.Enqueue(overChunk.get(), 0, 0, 0, 4096, 65, kTicket, receiver) == E_INVALIDARG,
           "over-one-mib-chunk-refused");

    auto gray = Texture(device.device.get(), 16, 8, DXGI_FORMAT_R8_UNORM);
    Expect(gray != nullptr, "gray-texture-created");
    Expect(gray && queue.Enqueue(gray.get(), 0, 15, 7, 1, 1, kTicket, receiver) == S_OK, "gray8-exact-edge-admitted");
    Expect(queue.Enqueue(basic.get(), 0, 0, 0, 1, 1, kTicket, receiver) == S_OK, "bgra8-admitted");

    std::array<wil::com_ptr_nothrow<ID3D11Texture2D>, kExpectedUploadCapacity> capacityTextures;
    bool allCreated = true;
    bool allAdmitted = true;
    for (std::size_t i = 0; i < capacityTextures.size(); ++i)
    {
        capacityTextures[i] = Texture(device.device.get(), 2, 2);
        allCreated = allCreated && capacityTextures[i] != nullptr;
        if (capacityTextures[i])
            allAdmitted = allAdmitted &&
                          SUCCEEDED(capacityQueue.Enqueue(capacityTextures[i].get(), 0, 0, 0, 2, 2, {i, 1}, receiver));
        else
            allAdmitted = false;
    }
    Expect(allCreated, "capacity-textures-created");
    Expect(allAdmitted, "eight-requests-admitted");
    Expect(capacityQueue.Enqueue(basic.get(), 0, 0, 0, 1, 1, {0, 2}, receiver) ==
               HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA),
           "ninth-request-refused");
    Expect(receiver.rowsCalls == 0 && receiver.finishedCalls == 0, "admission-cases-do-not-callback");

    queue.StopNotifications();
    capacityQueue.StopNotifications();
    scheduler.Close();
    queue.CloseAfterWorkers();
    capacityQueue.CloseAfterWorkers();
    Expect(!routes.HasPending() && !routes.HasReady(), "multiplexer-detached-on-close");
}

void TestFillLeaseRefusal()
{
    Device device;
    Device otherDevice;
    Expect(SUCCEEDED(device.Create()), "lease.device-created");
    Expect(SUCCEEDED(otherDevice.Create()), "lease.other-device-created");
    if (!device.device || !otherDevice.device)
        return;

    JobScheduler scheduler;
    OwnerWorkMultiplexer routes;
    Receiver receiver;
    GpuUploadQueue queue;
    GpuUploadQueue otherQueue;
    const bool initialized = scheduler.Initialize(1, {}, {StableMemory, nullptr}) == S_OK &&
                             scheduler.RegisterOwnerWork(routes) && SUCCEEDED(queue.Initialize(scheduler, &routes)) &&
                             SUCCEEDED(otherQueue.Initialize(scheduler, &routes)) &&
                             SUCCEEDED(queue.Bind(device.device.get(), device.context.get())) &&
                             SUCCEEDED(otherQueue.Bind(device.device.get(), device.context.get()));
    Expect(initialized, "lease.initialize-two-queues");
    if (!initialized)
    {
        queue.StopNotifications();
        otherQueue.StopNotifications();
        scheduler.Close();
        queue.CloseAfterWorkers();
        otherQueue.CloseAfterWorkers();
        return;
    }
    auto texture = Texture(device.device.get(), 2, 2);
    Expect(texture != nullptr, "lease.texture-created");
    if (texture)
    {
        Expect(!queue.PublishFilled({}, GpuUploadFill::Ready), "default-fill-lease-refused");
        Expect(!queue.PublishFilled({0, 1, std::numeric_limits<std::uint64_t>::max()}, GpuUploadFill::Ready),
               "foreign-queue-fill-lease-refused");
        Expect(SUCCEEDED(queue.Enqueue(texture.get(), 0, 0, 0, 2, 2, kTicket, receiver)), "lease.request-admitted");
        queue.Pump(); // One tiny WARP Map establishes the opaque lease; the test never reads or writes its bytes.
        Expect(receiver.rowsCalls == 1 && receiver.rowsTicket == kTicket && receiver.rows.data != nullptr,
               "lease-published-to-stable-receiver");
        if (receiver.rowsCalls == 1)
        {
            const GpuUploadLease lease = receiver.rows.lease;
            Expect(!otherQueue.PublishFilled(lease, GpuUploadFill::Ready), "wrong-queue-live-lease-refused");
            auto unpublished = lease;
            ++unpublished.generation;
            Expect(!queue.PublishFilled(unpublished, GpuUploadFill::Ready), "unpublished-generation-refused");
            Expect(queue.PublishFilled(lease, GpuUploadFill::Cancelled), "current-lease-cancel-published");
            Expect(!queue.PublishFilled(lease, GpuUploadFill::Ready), "duplicate-fill-publication-refused");
            queue.Pump();
            Expect(receiver.finishedCalls == 1 && receiver.finishedTicket == kTicket && receiver.result == E_ABORT,
                   "cancelled-lease-finishes-on-owner-pump");
        }
    }

    queue.StopNotifications();
    otherQueue.StopNotifications();
    scheduler.Close();
    queue.CloseAfterWorkers();
    otherQueue.CloseAfterWorkers();
}
} // namespace

int RunGpuUploadAdmissionTests()
{
    gFailures = 0;
    Device device;
    Device otherDevice;
    Expect(SUCCEEDED(device.Create()), "device-created");
    Expect(SUCCEEDED(otherDevice.Create()), "second-device-created");
    if (!device.device || !otherDevice.device)
        return gFailures;

    auto texture = Texture(device.device.get(), 4, 4);
    Expect(texture != nullptr, "pre-init-texture-created");
    if (texture)
        TestBeforeInitialization(texture.get());
    TestAdmission(device, otherDevice);
    TestFillLeaseRefusal();
    return gFailures;
}
