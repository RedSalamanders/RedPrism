#pragma once

#include "CanvasRenderer.h"
#include "GpuRecordingCommands.h"

#include <optional>

// One original admitted utility owns this private recorder/residency through
// serial regions. The parent joins recording and original command/copy/row uses before each handoff.
// This class provides no admission, completion fence, swap chain, Present or document mutation.
class RecordedUtilityGraph final
{
  public:
    explicit RecordedUtilityGraph(std::uint32_t originalOwnerThread) noexcept;
    ~RecordedUtilityGraph();
    RecordedUtilityGraph(const RecordedUtilityGraph&) = delete;
    RecordedUtilityGraph& operator=(const RecordedUtilityGraph&) = delete;
    [[nodiscard]] HRESULT PrepareForWork(ID3D11Device* device, CanvasDocument source, D3D11_BOX region,
                                         GpuCommandStream& original, CancelToken cancellation) noexcept;
    // Only after actual prior copy/row acknowledgement, original transport retirement and RestartOwner.
    // The recording policy must validate a fresh original production epoch, not an arbitrary bool or thread ID.
    [[nodiscard]] HRESULT RecordNextForWork(D3D11_BOX region, GpuCommandStream& original,
                                            CancelToken cancellation) noexcept;
    // Parent separately acknowledges the original recorder and every original command before this call.
    [[nodiscard]] HRESULT PublishRegionOwner(ID3D11DeviceContext* originalImmediate) noexcept;
    // Borrowed exclusively through the following actual staging copy; no composition/upload on the owner.
    [[nodiscard]] HRESULT BorrowRegionOwner(ID3D11Device* device, ID3D11DeviceContext* immediate,
                                            ID3D11Texture2D*& texture, D3D11_BOX& localBox) noexcept;
    // Excludes future borrows only; it never asserts copy/GPU/row acknowledgement or refunds admission.
    [[nodiscard]] bool ExcludeRegionOwner() noexcept;
    [[nodiscard]] bool RetireForWork() noexcept;
    [[nodiscard]] bool RetireUnpublishedForWork() noexcept;
    // Worker diagnostic only; cumulative byte count survives successful original-region restarts.
    [[nodiscard]] std::uint64_t RecordedUploadBytesForWork() const noexcept;

  private:
    enum class Phase : std::uint8_t
    {
        Empty,
        Recording,
        Sealed,
        Published,
        Excluded,
        Retired
    };
    [[nodiscard]] bool IsOwner() const noexcept;
    [[nodiscard]] bool ValidRegion(D3D11_BOX region) const noexcept;
    [[nodiscard]] HRESULT RecordRegionForWork(D3D11_BOX region) noexcept;
    void ReleaseForWork() noexcept;
    std::uint32_t _ownerThread;
    Phase _phase = Phase::Empty;
    bool _utilityAcquired = false;
    CanvasDocument _source;
    D3D11_BOX _region{};
    GpuCommandStream* _originalStream = nullptr;
    wil::com_ptr_nothrow<ID3D11Device> _device;
    wil::com_ptr_nothrow<ID3D11DeviceContext> _deferred;
    wil::com_ptr_nothrow<ID3D11DeviceContext> _immediate;
    wil::com_ptr_nothrow<ID3D11Texture2D> _regionTexture;
    std::optional<GpuRecordingCommands> _recording;
    CanvasRenderer _composition;
};
