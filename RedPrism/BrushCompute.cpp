#include "BrushCompute.h"

#include "BrushCoverageShader.h"
#include "GpuRecordingCommands.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <utility>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
constexpr std::size_t kMinimumSegmentCapacity = 64;
} // namespace

GpuBrushCoverage::~GpuBrushCoverage()
{
    Release();
}

HRESULT GpuBrushCoverage::Initialize(ID3D11Device* device, ID3D11DeviceContext* context) noexcept
{
    static_assert(sizeof(Uniforms) % 16 == 0);
    Release();
    if (device == nullptr || context == nullptr)
    {
        return E_INVALIDARG;
    }
    HRESULT result =
        device->CreateComputeShader(g_BrushCoverageShader, sizeof(g_BrushCoverageShader), nullptr, _pipeline.put());
    if (FAILED(result))
    {
        return result;
    }
    D3D11_BUFFER_DESC uniforms{};
    uniforms.ByteWidth = sizeof(Uniforms);
    uniforms.Usage = D3D11_USAGE_DYNAMIC;
    uniforms.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    uniforms.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    result = device->CreateBuffer(&uniforms, nullptr, _uniforms.put());
    if (FAILED(result))
    {
        _pipeline.reset();
        return result;
    }
    _device = device;
    _context = context;
    return S_OK;
}

void GpuBrushCoverage::Release() noexcept
{
    _tiles.clear();
    _tileGpuBytes = 0;
    _segmentsView.reset();
    _segments.reset();
    _segmentCapacity = 0;
    _uniforms.reset();
    _pipeline.reset();
    _device = nullptr;
    _context = nullptr;
    _recording = nullptr;
}

void GpuBrushCoverage::BeginStroke() noexcept
{
    _tiles.clear();
    _tileGpuBytes = 0;
}

void GpuBrushCoverage::EndStroke() noexcept
{
    _tiles.clear();
    _tileGpuBytes = 0;
}

GpuBrushCoverage::StrokeEpoch::StrokeEpoch() : _tiles(std::make_unique<TileMap>()) {}

GpuBrushCoverage::StrokeEpoch::~StrokeEpoch()
{
    ResetForWork();
}

bool GpuBrushCoverage::StrokeEpoch::HasResources() const noexcept
{
    return (_tiles && !_tiles->empty()) || _segments || _segmentsView;
}

void GpuBrushCoverage::StrokeEpoch::ResetForWork() noexcept
{
    assert(!_capturingThread || GetCurrentThreadId() != _capturingThread);
    // Destroy the directory as well as its nodes on this worker. clear() alone retains large bucket storage.
    _tiles.reset();
    _segmentsView.reset();
    _segments.reset();
    _device.reset();
    _segmentCapacity = 0;
    _tileGpuBytes = 0;
    _cpuBytes = 0;
    _gpuBytes = 0;
    _capturingThread = 0;
}

std::uint64_t GpuBrushCoverage::StrokeCpuOwnershipBytes() const noexcept
{
    // MSVC's hash directory has two iterator slots per bucket. Nodes/control/proxies and opaque resource
    // wrappers receive separate conservative allowances; payload buffers are charged by the GPU graph.
    constexpr std::uint64_t metadata = sizeof(StrokeEpoch) + 64 * 1024;
    constexpr auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    constexpr std::uint64_t node = sizeof(std::pair<const std::uint64_t, TileBuffers>) + 256 + 5 * 4096;
    if (_tiles.bucket_count() > (maximum - metadata) / (2 * sizeof(void*)))
        return 0;
    const auto base = metadata + _tiles.bucket_count() * 2 * sizeof(void*);
    return _tiles.size() <= (maximum - base) / node ? base + _tiles.size() * node : 0;
}

std::uint64_t GpuBrushCoverage::StrokeGpuOwnershipBytes() const noexcept
{
    constexpr auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    constexpr std::uint64_t allowance = 2 * 4096;
    if (!_segments)
        return _tileGpuBytes;
    if (_tileGpuBytes > maximum - allowance ||
        _segmentCapacity > (maximum - _tileGpuBytes - allowance) / (4 * sizeof(float)))
        return 0;
    return _tileGpuBytes + allowance + _segmentCapacity * 4 * sizeof(float);
}

bool GpuBrushCoverage::CanTakeStrokeEpoch(const StrokeEpoch& empty) const noexcept
{
    if (!empty._tiles || empty.HasResources() || empty._capturingThread)
        return false;
    const auto cpu = StrokeCpuOwnershipBytes();
    const auto gpu = StrokeGpuOwnershipBytes();
    return cpu != 0 && (gpu != 0 || (_tiles.empty() && !_segments));
}

bool GpuBrushCoverage::TakeStrokeEpoch(StrokeEpoch& empty) noexcept
{
    if (!CanTakeStrokeEpoch(empty))
        return false;
    const auto cpu = StrokeCpuOwnershipBytes();
    const auto gpu = StrokeGpuOwnershipBytes();
    static_assert(noexcept(_tiles.swap(*empty._tiles)));
    _tiles.swap(*empty._tiles);
    _segments.swap(empty._segments);
    _segmentsView.swap(empty._segmentsView);
    empty._device = _device; // Own the identity; a released device address cannot alias a replacement.
    empty._segmentCapacity = std::exchange(_segmentCapacity, 0);
    empty._tileGpuBytes = std::exchange(_tileGpuBytes, 0);
    empty._cpuBytes = cpu;
    empty._gpuBytes = gpu;
    empty._capturingThread = GetCurrentThreadId();
    return true;
}

bool GpuBrushCoverage::RestoreStrokeEpoch(StrokeEpoch& epoch) noexcept
{
    if (!epoch._tiles || !epoch._capturingThread || epoch._capturingThread != GetCurrentThreadId() || !_device ||
        epoch._device.get() != _device || FAILED(_device->GetDeviceRemovedReason()) || !_tiles.empty() || _segments ||
        _segmentsView || _segmentCapacity || _tileGpuBytes || !epoch._cpuBytes)
        return false;
    _tiles.swap(*epoch._tiles);
    _segments.swap(epoch._segments);
    _segmentsView.swap(epoch._segmentsView);
    _segmentCapacity = std::exchange(epoch._segmentCapacity, 0);
    _tileGpuBytes = std::exchange(epoch._tileGpuBytes, 0);
    // Empty directory capacity is real retained CPU state. Its final release stays on the worker;
    // keeping the captured device reference there also avoids a last COM reference on this owner.
    epoch._cpuBytes = sizeof(StrokeEpoch) + 64 * 1024 + epoch._tiles->bucket_count() * 2 * sizeof(void*);
    epoch._gpuBytes = 0;
    epoch._capturingThread = 0;
    return true;
}

HRESULT GpuBrushCoverage::EnsureSegments(std::size_t count) noexcept
{
    const std::size_t needed = std::max(kMinimumSegmentCapacity, count);
    if (_segments && _segmentCapacity >= needed)
    {
        return S_OK;
    }
    std::size_t capacity = std::max(kMinimumSegmentCapacity, _segmentCapacity);
    while (capacity < needed)
    {
        capacity *= 2;
    }
    _segmentsView.reset();
    _segments.reset();
    D3D11_BUFFER_DESC description{};
    description.ByteWidth = static_cast<UINT>(capacity * 4u * sizeof(float));
    description.Usage = D3D11_USAGE_DYNAMIC;
    description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    description.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    description.StructureByteStride = 4u * sizeof(float);
    HRESULT result = _device->CreateBuffer(&description, nullptr, _segments.put());
    if (FAILED(result))
    {
        return result;
    }
    D3D11_SHADER_RESOURCE_VIEW_DESC view{};
    view.Format = DXGI_FORMAT_UNKNOWN;
    view.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    view.Buffer.FirstElement = 0;
    view.Buffer.NumElements = static_cast<UINT>(capacity);
    result = _device->CreateShaderResourceView(_segments.get(), &view, _segmentsView.put());
    if (FAILED(result))
    {
        _segments.reset();
        return result;
    }
    _segmentCapacity = capacity;
    return S_OK;
}

GpuBrushCoverage::TileBuffers* GpuBrushCoverage::BuffersFor(const BrushKernelTile& tile) noexcept
{
    const auto found = _tiles.find(tile.key);
    if (found != _tiles.end())
    {
        return found->second.width == tile.width && found->second.height == tile.height ? &found->second : nullptr;
    }
    const UINT pixels = static_cast<UINT>(tile.width) * static_cast<UINT>(tile.height);
    if (pixels == 0)
    {
        return nullptr;
    }
    TileBuffers buffers;
    buffers.width = tile.width;
    buffers.height = tile.height;
    D3D11_BUFFER_DESC description{};
    description.ByteWidth = pixels * sizeof(float);
    description.Usage = D3D11_USAGE_DEFAULT;
    description.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    description.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    description.StructureByteStride = sizeof(float);
    if (FAILED(_device->CreateBuffer(&description, nullptr, buffers.permanent.put())))
    {
        return nullptr;
    }
    D3D11_UNORDERED_ACCESS_VIEW_DESC view{};
    view.Format = DXGI_FORMAT_UNKNOWN;
    view.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    view.Buffer.FirstElement = 0;
    view.Buffer.NumElements = pixels;
    if (FAILED(_device->CreateUnorderedAccessView(buffers.permanent.get(), &view, buffers.permanentView.put())))
    {
        return nullptr;
    }
    // Structured DXGI_FORMAT_UNKNOWN UAVs accept the uint clear; zero bits are float density zero.
    // A fresh tile now records a constant-size owner command instead of allocating/copying CPU pixel zeros.
    static_assert(sizeof(float) == sizeof(std::uint32_t));
    constexpr UINT zeros[4] = {0, 0, 0, 0};
    _context->ClearUnorderedAccessViewUint(buffers.permanentView.get(), zeros);
    description.ByteWidth = pixels * sizeof(std::uint32_t);
    description.StructureByteStride = sizeof(std::uint32_t);
    if (FAILED(_device->CreateBuffer(&description, nullptr, buffers.preview.put())) ||
        FAILED(_device->CreateUnorderedAccessView(buffers.preview.get(), &view, buffers.previewView.put())))
    {
        return nullptr;
    }
    try
    {
        // Logical density/uint payload plus four opaque buffer/view allowances. Cache only after insertion.
        const auto bytes = static_cast<std::uint64_t>(pixels) * 8 + 4 * 4096;
        if (bytes > (std::numeric_limits<std::uint64_t>::max)() - _tileGpuBytes)
            return nullptr;
        auto* result = &_tiles.emplace(tile.key, std::move(buffers)).first->second;
        _tileGpuBytes += bytes;
        return result;
    }
    catch (const std::bad_alloc&)
    {
        return nullptr;
    }
}

HRESULT GpuBrushCoverage::EnsureStaging(TileBuffers& buffers) noexcept
{
    if (buffers.staging)
        return S_OK;
    D3D11_BUFFER_DESC staging{};
    staging.ByteWidth = static_cast<UINT>(buffers.width) * static_cast<UINT>(buffers.height) * sizeof(std::uint32_t);
    staging.Usage = D3D11_USAGE_STAGING;
    staging.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    const auto bytes = static_cast<std::uint64_t>(staging.ByteWidth) + 4096;
    if (bytes > (std::numeric_limits<std::uint64_t>::max)() - _tileGpuBytes)
        return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
    const HRESULT result = _device->CreateBuffer(&staging, nullptr, buffers.staging.put());
    if (SUCCEEDED(result))
        _tileGpuBytes += bytes;
    return result;
}

bool GpuBrushCoverage::Render(std::span<const BrushKernelTile> tiles, std::span<const BrushSegment> settled,
                              std::span<const BrushSegment> tail, const BrushKernelParams& params) noexcept
{
    return RenderImpl(tiles, settled, tail, params, true);
}

bool GpuBrushCoverage::RecordCoverage(std::span<const BrushKernelTile> tiles, std::span<const BrushSegment> settled,
                                      std::span<const BrushSegment> tail, const BrushKernelParams& params) noexcept
{
    return RenderImpl(tiles, settled, tail, params, false);
}

bool GpuBrushCoverage::RecordEpochCoverage(StrokeEpoch& epoch, std::span<const BrushKernelTile> tiles,
                                           std::span<const BrushSegment> settled,
                                           const BrushKernelParams& params) noexcept
{
    if (!Ready() || !epoch._tiles || epoch._capturingThread != GetCurrentThreadId() || epoch._device.get() != _device ||
        !_tiles.empty() || _segments || _segmentsView || _segmentCapacity || _tileGpuBytes ||
        FAILED(_device->GetDeviceRemovedReason()))
        return false;
    const auto swapGraph = [&]() noexcept
    {
        _tiles.swap(*epoch._tiles);
        _segments.swap(epoch._segments);
        _segmentsView.swap(epoch._segmentsView);
        std::swap(_segmentCapacity, epoch._segmentCapacity);
        std::swap(_tileGpuBytes, epoch._tileGpuBytes);
    };
    swapGraph();
    const auto restore = wil::scope_exit(
        [&]() noexcept
        {
            // Failed resource creation can still leave a private prefix. Cache its whole charge before restoring
            // the graph; the same original final job retires it after the actual GPU completion acknowledgement.
            epoch._cpuBytes = StrokeCpuOwnershipBytes();
            epoch._gpuBytes = StrokeGpuOwnershipBytes();
            swapGraph();
        });
    return RecordCoverage(tiles, settled, {}, params);
}

HRESULT GpuBrushCoverage::RecordEpochForWork(StrokeEpoch& epoch, ID3D11DeviceContext* deferred,
                                             GpuRecordingCommands& commands, std::span<const BrushKernelTile> tiles,
                                             std::span<const BrushSegment> settled, std::span<const BrushSegment> tail,
                                             const BrushKernelParams& params)
{
    if (!epoch._tiles || !epoch._device || !epoch._capturingThread || epoch._capturingThread == GetCurrentThreadId() ||
        !deferred || deferred->GetType() != D3D11_DEVICE_CONTEXT_DEFERRED)
        return E_INVALIDARG;
    const auto bound = commands.ContextStatusForWork(epoch._device.get(), deferred);
    if (FAILED(bound))
        return bound;
    const auto deviceStatus = epoch._device->GetDeviceRemovedReason();
    if (FAILED(deviceStatus))
        return deviceStatus;
    const auto finiteFloat = [](double value) noexcept
    { return std::isfinite(value) && std::abs(value) <= (std::numeric_limits<float>::max)(); };
    if (settled.size() > 1024 || tail.size() > 1 || !finiteFloat(params.radius) ||
        static_cast<float>(params.radius) <= 0 || !std::isfinite(params.hardness) || params.hardness < 0 ||
        params.hardness > 1 || !finiteFloat(params.spacing) || static_cast<float>(params.spacing) <= 0 ||
        !finiteFloat(params.antialiasWidth) || static_cast<float>(params.antialiasWidth) <= 0 ||
        !std::isfinite(params.canvasWidth) || params.canvasWidth <= 0 || params.canvasWidth > kMaxCanvasSide ||
        !std::isfinite(params.canvasHeight) || params.canvasHeight <= 0 || params.canvasHeight > kMaxCanvasSide ||
        !std::isfinite(params.pixelToDocument.a) || !std::isfinite(params.pixelToDocument.b) ||
        !std::isfinite(params.pixelToDocument.c) || !std::isfinite(params.pixelToDocument.d) ||
        !std::isfinite(params.pixelToDocument.tx) || !std::isfinite(params.pixelToDocument.ty))
        return E_INVALIDARG;
    std::optional<std::uint64_t> previousKey;
    for (const auto& tile : tiles)
    {
        if (tile.width <= 0 || tile.height <= 0 || tile.width > BrushStroke::kTileSize ||
            tile.height > BrushStroke::kTileSize || tile.x < 0 || tile.y < 0 ||
            tile.x > BrushStroke::kMaxGridSide - tile.width || tile.y > BrushStroke::kMaxGridSide - tile.height ||
            (previousKey && tile.key <= previousKey.value()))
            return E_INVALIDARG;
        previousKey = tile.key; // Prepared coverage supplies a sorted exact union, without duplicate deliveries.
        for (const double y : {static_cast<double>(tile.y), static_cast<double>(tile.y) + tile.height})
            for (const double x : {static_cast<double>(tile.x), static_cast<double>(tile.x) + tile.width})
            {
                const auto corner = params.pixelToDocument.Apply({x, y});
                if (!finiteFloat(corner.x) || !finiteFloat(corner.y))
                    return E_INVALIDARG;
            }
    }
    const auto finiteSegments = [&finiteFloat](std::span<const BrushSegment> segments) noexcept
    {
        for (const auto& segment : segments)
            if (!finiteFloat(segment.from.x) || !finiteFloat(segment.from.y) || !finiteFloat(segment.to.x) ||
                !finiteFloat(segment.to.y))
                return false;
        return true;
    };
    if (!finiteSegments(settled) || !finiteSegments(tail))
        return E_INVALIDARG;
    GpuBrushCoverage privateEngine;
    const HRESULT initialized = privateEngine.Initialize(epoch._device.get(), deferred);
    if (FAILED(initialized))
        return initialized;
    privateEngine._recording = &commands;
    const auto swapGraph = [&]() noexcept
    {
        privateEngine._tiles.swap(*epoch._tiles);
        privateEngine._segments.swap(epoch._segments);
        privateEngine._segmentsView.swap(epoch._segmentsView);
        std::swap(privateEngine._segmentCapacity, epoch._segmentCapacity);
        std::swap(privateEngine._tileGpuBytes, epoch._tileGpuBytes);
    };
    swapGraph();
    const auto restore = wil::scope_exit(
        [&]() noexcept
        {
            epoch._cpuBytes = privateEngine.StrokeCpuOwnershipBytes();
            epoch._gpuBytes = privateEngine.StrokeGpuOwnershipBytes();
            swapGraph(); // Captured resources outlive the private pipeline, even after a failed prefix.
        });
    const bool recorded = privateEngine.RenderImpl(tiles, settled, tail, params, false);
    const auto commandStatus = commands.StatusForWork();
    return FAILED(commandStatus) ? commandStatus : recorded ? S_OK : E_FAIL;
}

ID3D11Buffer* GpuBrushCoverage::RecordedEpochBufferOwner(const StrokeEpoch& epoch, ID3D11Device* originalDevice,
                                                         std::uint64_t key, int width, int height) noexcept
{
    if (!epoch._tiles || !originalDevice || epoch._device.get() != originalDevice ||
        epoch._capturingThread != GetCurrentThreadId())
        return nullptr;
    const auto found = epoch._tiles->find(key);
    return found != epoch._tiles->end() && found->second.width == width && found->second.height == height
               ? found->second.preview.get()
               : nullptr;
}

ID3D11Buffer* GpuBrushCoverage::EpochCoverageBuffer(const StrokeEpoch& epoch, std::uint64_t key, int width,
                                                    int height) const noexcept
{
    if (!epoch._tiles || epoch._capturingThread != GetCurrentThreadId() || epoch._device.get() != _device)
        return nullptr;
    const auto found = epoch._tiles->find(key);
    return found != epoch._tiles->end() && found->second.width == width && found->second.height == height
               ? found->second.preview.get()
               : nullptr;
}

ID3D11Buffer* GpuBrushCoverage::CoverageBuffer(std::uint64_t key, int width, int height) const noexcept
{
    const auto found = _tiles.find(key);
    return found != _tiles.end() && found->second.width == width && found->second.height == height
               ? found->second.preview.get()
               : nullptr;
}

bool GpuBrushCoverage::RenderImpl(std::span<const BrushKernelTile> tiles, std::span<const BrushSegment> settled,
                                  std::span<const BrushSegment> tail, const BrushKernelParams& params,
                                  bool readback) noexcept
{
    if (!Ready() || tiles.empty())
    {
        return Ready();
    }
    // A dummy segment supplies a valid buffer for tail removal with no new geometry.
    const std::size_t total = settled.size() + tail.size();
    if (FAILED(EnsureSegments(std::max<std::size_t>(1, total))))
    {
        return false;
    }
    const auto uploadSegments = [&]() noexcept
    {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(_context->Map(_segments.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            return false;
        }
        float* segments = static_cast<float*>(mapped.pData);
        std::size_t written = 0;
        for (const BrushSegment& segment : settled)
        {
            segments[written * 4] = static_cast<float>(segment.from.x);
            segments[written * 4 + 1] = static_cast<float>(segment.from.y);
            segments[written * 4 + 2] = static_cast<float>(segment.to.x);
            segments[written * 4 + 3] = static_cast<float>(segment.to.y);
            ++written;
        }
        for (const BrushSegment& segment : tail)
        {
            segments[written * 4] = static_cast<float>(segment.from.x);
            segments[written * 4 + 1] = static_cast<float>(segment.from.y);
            segments[written * 4 + 2] = static_cast<float>(segment.to.x);
            segments[written * 4 + 3] = static_cast<float>(segment.to.y);
            ++written;
        }
        if (written == 0)
        {
            std::memset(segments, 0, 4 * sizeof(float));
        }
        _context->Unmap(_segments.get(), 0);

        return true;
    };
    if (!_recording && !uploadSegments())
        return false;
    if (!_recording)
    {
        _context->CSSetShader(_pipeline.get(), nullptr, 0);
        ID3D11ShaderResourceView* resources[] = {_segmentsView.get()};
        _context->CSSetShaderResources(0, 1, resources);
        ID3D11Buffer* constants[] = {_uniforms.get()};
        _context->CSSetConstantBuffers(0, 1, constants);
    }
    bool ok = true;
    for (const BrushKernelTile& tile : tiles)
    {
        TileBuffers* buffers = BuffersFor(tile);
        if (buffers == nullptr || (readback && FAILED(EnsureStaging(*buffers))))
        {
            ok = false;
            break;
        }
        if (_recording)
        {
            _recording->CheckpointForWork(); // Includes a new density clear before any pixel reads it.
            if (FAILED(_recording->StatusForWork()))
                return false;
        }
        const Vec2 origin =
            params.pixelToDocument.Apply(Vec2{static_cast<double>(tile.x), static_cast<double>(tile.y)});
        Uniforms uniforms{};
        uniforms.mapping[0] = static_cast<float>(params.pixelToDocument.a);
        uniforms.mapping[1] = static_cast<float>(params.pixelToDocument.b);
        uniforms.mapping[2] = static_cast<float>(params.pixelToDocument.c);
        uniforms.mapping[3] = static_cast<float>(params.pixelToDocument.d);
        uniforms.geometry[0] = static_cast<float>(origin.x);
        uniforms.geometry[1] = static_cast<float>(origin.y);
        uniforms.geometry[2] = static_cast<float>(params.radius);
        uniforms.geometry[3] = static_cast<float>(params.hardness);
        uniforms.canvas[0] = static_cast<float>(params.canvasWidth);
        uniforms.canvas[1] = static_cast<float>(params.canvasHeight);
        uniforms.canvas[2] = static_cast<float>(params.antialiasWidth);
        uniforms.canvas[3] = static_cast<float>(params.spacing);
        uniforms.counts[0] = static_cast<std::uint32_t>(tile.width);
        uniforms.counts[1] = static_cast<std::uint32_t>(tile.height);
        uniforms.counts[2] = static_cast<std::uint32_t>(settled.size());
        uniforms.counts[3] = static_cast<std::uint32_t>(total);
        // Keep each pixel's original full-tile origin and all ordered segments. Splitting the segment
        // sum would alter settled density rounding/capping; only disjoint pixel regions are dispatched.
        const int side = _recording ? (total <= 16 ? 256 : total <= 64 ? 128 : total <= 256 ? 64 : 32) : 256;
        for (int y = 0; y < tile.height && ok; y += side)
        {
            for (int x = 0; x < tile.width; x += side)
            {
                if (_recording && FAILED(_recording->ContextStatusForWork(_device, _context)))
                {
                    ok = false;
                    break;
                }
                const int endX = std::min(tile.width, x + side);
                const int endY = std::min(tile.height, y + side);
                uniforms.dispatchBounds[0] = static_cast<std::uint32_t>(x);
                uniforms.dispatchBounds[1] = static_cast<std::uint32_t>(y);
                uniforms.dispatchBounds[2] = static_cast<std::uint32_t>(endX);
                uniforms.dispatchBounds[3] = static_cast<std::uint32_t>(endY);
                // Dynamic maps are command-list local. Record the exact segment data in the same list
                // as its dispatch; never rely on a mapped alias from an earlier finished command list.
                if (_recording && !uploadSegments())
                {
                    ok = false;
                    break;
                }
                D3D11_MAPPED_SUBRESOURCE mappedUniforms{};
                if (FAILED(_context->Map(_uniforms.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedUniforms)))
                {
                    ok = false;
                    break;
                }
                std::memcpy(mappedUniforms.pData, &uniforms, sizeof(uniforms));
                _context->Unmap(_uniforms.get(), 0);
                // FinishCommandList(FALSE) leaves defaults; every streamed region rebinds its complete state.
                if (_recording)
                {
                    _context->CSSetShader(_pipeline.get(), nullptr, 0);
                    ID3D11ShaderResourceView* resources[] = {_segmentsView.get()};
                    _context->CSSetShaderResources(0, 1, resources);
                    ID3D11Buffer* constants[] = {_uniforms.get()};
                    _context->CSSetConstantBuffers(0, 1, constants);
                }
                ID3D11UnorderedAccessView* views[] = {buffers->permanentView.get(), buffers->previewView.get()};
                _context->CSSetUnorderedAccessViews(0, 2, views, nullptr);
                _context->Dispatch(static_cast<UINT>((endX - x + 15) / 16), static_cast<UINT>((endY - y + 15) / 16), 1);
                ID3D11UnorderedAccessView* none[] = {nullptr, nullptr};
                _context->CSSetUnorderedAccessViews(0, 2, none, nullptr);
                if (_recording)
                {
                    ID3D11ShaderResourceView* noResources[] = {nullptr};
                    _context->CSSetShaderResources(0, 1, noResources);
                    _context->CSSetShader(nullptr, nullptr, 0);
                    _recording->CheckpointForWork();
                }
            }
        }
        if (readback)
            _context->CopyResource(buffers->staging.get(), buffers->preview.get());
    }
    ID3D11ShaderResourceView* noResources[] = {nullptr};
    _context->CSSetShaderResources(0, 1, noResources);
    _context->CSSetShader(nullptr, nullptr, 0);
    if (!ok || !readback)
    {
        return ok;
    }
    // The previews come back as one uint per pixel; the stroke composes tiles from bytes.
    for (const BrushKernelTile& tile : tiles)
    {
        TileBuffers* buffers = BuffersFor(tile);
        if (buffers == nullptr || tile.preview == nullptr)
        {
            return false;
        }
        D3D11_MAPPED_SUBRESOURCE read{};
        if (FAILED(_context->Map(buffers->staging.get(), 0, D3D11_MAP_READ, 0, &read)))
        {
            return false;
        }
        const std::uint32_t* values = static_cast<const std::uint32_t*>(read.pData);
        const std::size_t pixels = static_cast<std::size_t>(tile.width) * static_cast<std::size_t>(tile.height);
        for (std::size_t index = 0; index < pixels; ++index)
        {
            tile.preview[index] = static_cast<std::uint8_t>(std::min<std::uint32_t>(255u, values[index]));
        }
        _context->Unmap(buffers->staging.get(), 0);
    }
    return true;
}
