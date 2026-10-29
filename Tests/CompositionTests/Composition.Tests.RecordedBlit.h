#pragma once

class CanvasRenderer;
struct ID3D11Device;
struct ID3D11DeviceContext;

// The parent supplies real sealed graphs after producer join, actual original-stream acknowledgments,
// and BindAcknowledgedDisplayOwner on the same immediate context. No ownership transfers here.
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4623 4626 5027)
struct RecordedBlitGraphHarness final
{
    CanvasRenderer& composition;
    ID3D11Device* device;
    ID3D11DeviceContext* immediateContext;
};
#pragma warning(pop)

// Pair: opaque black/white 2x1 display graph. Stripes: opaque alternating 64x64 column graph with ready mips.
// Tiled: an actual sealed graph whose viewed center is opaque; all three contexts are their acknowledged owner
// immediate contexts. No command-list pipeline state is assumed to persist after ExecuteCommandList(FALSE).
[[nodiscard]] int RunRecordedBlitTests(RecordedBlitGraphHarness& pair, RecordedBlitGraphHarness& stripes,
                                       RecordedBlitGraphHarness& tiled);
