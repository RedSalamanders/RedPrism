#pragma once

#include "CanvasRenderer.h"
#include "GpuRecordingCommands.h"

#include <optional>

// Complete owning display candidate. Committed, stroke and adjustment parents admit their separate roles.
// The parent admits every source/container/device/command owner
// before Prepare, joins its original recorder and acknowledges all original GPU batches before Publish, and
// excludes future frame/seed borrows plus acknowledges the last old-frame use before Detached retirement.
// This graph owns its policy/context/root: no stack policy or borrowed layer records survive a producer.
class RecordedDisplayGraph final
{
  public:
    // A worker-allocated admitted root binds its original dispatcher before any preparation.
    explicit RecordedDisplayGraph(std::uint32_t originalOwnerThread = 0) noexcept;
    ~RecordedDisplayGraph();
    RecordedDisplayGraph(const RecordedDisplayGraph&) = delete;
    RecordedDisplayGraph& operator=(const RecordedDisplayGraph&) = delete;
    [[nodiscard]] HRESULT PrepareForWork(ID3D11Device* device, CanvasDocument source, GpuCommandStream& stream,
                                         CancelToken cancellation,
                                         const RecordedDisplayGraph* publishedSeed = nullptr) noexcept;
    // The exclusive sample/final parent owns preview/tiles through recording and all actual GPU uses.
    // A stable published stroke seed retains the last complete canvas and working grid; only changed
    // tiles are uploaded. The new graph owns a private grid copy so a failure cannot damage that seed.
    [[nodiscard]] HRESULT PrepareStrokeForWork(ID3D11Device* device, CanvasDocument source,
                                               const CanvasRenderer::StrokePreview& preview,
                                               std::span<const RasterTile> tiles, GpuCommandStream& stream,
                                               CancelToken cancellation,
                                               const RecordedDisplayGraph* publishedSeed = nullptr) noexcept;
    // Exclusive original preview parent. Source metadata is already prepared on its admitted worker.
    // The adjustment/coverage owners are retained by this private graph; published seeds are read-only.
    // The parent separately admits the preview target, coverage and lookup in addition to the document.
    [[nodiscard]] HRESULT PrepareAdjustmentForWork(ID3D11Device* device, CanvasDocument source,
                                                   const CanvasRenderer::AdjustmentPreview& preview,
                                                   GpuCommandStream& stream, CancelToken cancellation,
                                                   const RecordedDisplayGraph* publishedSeed = nullptr) noexcept;
    [[nodiscard]] bool InheritedStrokeForTestOwner() const noexcept;
    // Parent calls only after the actual scheduler recording callback joined and every original batch is
    // acknowledged. This operation validates device identity, but establishes no join/completion by itself.
    [[nodiscard]] HRESULT PublishOwner(ID3D11DeviceContext* originalContext) noexcept;
    [[nodiscard]] HRESULT BlitOwner(ID3D11RenderTargetView* target, unsigned int width, unsigned int height,
                                    const CanvasViewport& view, float holeLeft, float holeTop, float holeWidth,
                                    float holeHeight) noexcept;
    [[nodiscard]] bool DetachOwner() noexcept;
    // After parent last-use acknowledgement. Workers release all policy, context, resources and source owners.
    [[nodiscard]] bool RetireForWork() noexcept;
    // Exclusive original worker stage after all recorded uses acknowledge, before publication. Cancellation
    // may skip useful Run; this path still destroys partial/sealed/empty private graphs off the owner.
    [[nodiscard]] bool RetireUnpublishedForWork() noexcept;
    [[nodiscard]] std::uint64_t SurfaceBytesOwner() const noexcept;
    [[nodiscard]] bool IsPublishedOwner() const noexcept;
    [[nodiscard]] bool IsPublishedOnOwner(ID3D11Device* device, ID3D11DeviceContext* context) const noexcept;
    // Diagnostics only after Publish; production consumers get a separately admitted committed utility graph.
    [[nodiscard]] CanvasRenderer* CompositionForTestOwner() noexcept;
    [[nodiscard]] ID3D11Device* DeviceForTestOwner() const noexcept;
    [[nodiscard]] ID3D11DeviceContext* ContextForTestOwner() const noexcept;
    [[nodiscard]] bool IsUnpreparedForTest() const noexcept
    {
        return _phase == Phase::Empty;
    }

  private:
    enum class Phase
    {
        Empty,
        Preparing,
        Sealed,
        Published,
        Detached,
        Retired
    };
    [[nodiscard]] HRESULT PrepareInternalForWork(ID3D11Device* device, CanvasDocument source, GpuCommandStream& stream,
                                                 CancelToken cancellation, const RecordedDisplayGraph* publishedSeed,
                                                 const CanvasRenderer::StrokePreview* preview,
                                                 std::span<const RasterTile> tiles,
                                                 const CanvasRenderer::AdjustmentPreview* adjustment) noexcept;
    [[nodiscard]] bool IsOwner() const noexcept;
    void ReleaseForWork() noexcept;
    std::uint32_t _ownerThread = 0;
    Phase _phase = Phase::Empty; // Original job-stage acknowledgements establish each cross-thread handoff.
    CanvasDocument _source;
    bool _inheritedStroke = false;
    wil::com_ptr_nothrow<ID3D11Device> _device;
    wil::com_ptr_nothrow<ID3D11DeviceContext> _deferred;
    wil::com_ptr_nothrow<ID3D11DeviceContext> _original;
    std::optional<GpuRecordingCommands> _recording; // Emplaced/destroyed only inside acknowledged worker stages.
    CanvasRenderer _composition;                    // Destroy before the policies/contexts/immutable source it borrows.
};
