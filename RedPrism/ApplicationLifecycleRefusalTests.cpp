#include "Application.h"

#include "ChromeHost.h"

#include <array>
#include <cstdio>
#include <limits>
#include <utility>

int Application::RunDocumentLifecycleRefusalSelfTest() noexcept
{
    unsigned int checks = 0, failures = 0;
    const auto expect = [&](bool condition, const char* name) noexcept
    {
        ++checks;
        failures += condition ? 0u : 1u;
        std::printf("%s document-lifecycle-refusal.%s\n", condition ? "PASS" : "FAIL", name);
    };
    const auto settle = [&]() noexcept
    { return SettleJobs(_jobs, 15000, DispatchMessageThunk, this) == SettleResult::Settled; };
    if (!settle())
        return 5;
    CancelTransformPreviewJobs();
    if (!settle())
        return 5;
    const std::array<std::array<double, 2>, 9> invalid{std::array<double, 2>{0, 7},
                                                       {-1, 7},
                                                       {5, 0},
                                                       {5, -1},
                                                       {30000.5, 7},
                                                       {5, 30000.5},
                                                       {(std::numeric_limits<double>::quiet_NaN)(), 7},
                                                       {5, (std::numeric_limits<double>::infinity)()},
                                                       {(std::numeric_limits<double>::max)(), 7}};
    for (const auto dimensions : invalid)
    {
        CanvasDocument document;
        document.id = Guid::Generate();
        document.width = 2;
        document.height = 2;
        constexpr std::array<std::uint8_t, 4> pixel{23, 47, 89, 255};
        ImageLayer layer = ImageLayer::Blank(L"Invalid canvas source", Vec2{1, 1});
        layer.asset =
            RasterAsset{Raster::Make(1, 1, RasterFormat::Bgra8888, pixel.data(), pixel.size()), {}, layer.name};
        const auto active = layer.id;
        document.layers.push_back(std::move(layer));
        _session.ReplaceDocument(std::move(document), active);
        _session.AddBlankLayer();
        _session.Undo();
        _session.history.MarkSaved();
        _documentPath = L"invalid-lifecycle-source.prism";
        if (!_session.BeginDeferredTransform(true))
            return 5;
        auto draft = _session.PendingTransformEdit()->draft;
        draft.origin = {3, 4};
        _session.PreviewTransform(draft);
        _sizeDialog.kind = SizeCardKind::NewCanvas;
        _sizeDialog.newWidth = dimensions[0];
        _sizeDialog.newHeight = dimensions[1];
        if (_sizeInvocation == UINT64_MAX)
            return 5;
        ++_sizeInvocation;
        SyncChrome();
        const auto source = _session.Document();
        const auto history = _session.history.CaptureState();
        const auto revision = _session.history.CurrentRevision();
        const auto generation = _session.Generation();
        const auto serial = _session.DocumentSerial();
        const auto opening = _session.PendingTransformEdit()->opening;
        const auto draftRevision = _session.TransformDraftRevision();
        const auto notices = _errorsShown;
        const auto receipt = QueueHostInput(HostSizeApplyInput{_sizeDialog, _sizeInvocation});
        if (!settle())
            return 5;
        expect(receipt.Read().outcome == HostOutcome::Failed && receipt.Read().error == E_INVALIDARG,
               "invalid-accepted-dimensions-refuse-before-destructive-prerequisites");
        expect(!_documentLifecycleTicket && !_documentLifecycleStamp && !_historyWork.Busy() &&
                   !_jobs.ReservedMemoryBytes() && !_jobs.ReleasingMemoryBytes(),
               "invalid-dimensions-admit-no-replacement-graph-and-retire-input");
        expect(_session.Document() == source && _session.Generation() == generation &&
                   _session.DocumentSerial() == serial && _session.ActiveLayerID() == active,
               "invalid-dimensions-preserve-complete-source-and-lifetime");
        expect(_session.history.IsCurrent(history) && _session.history.CurrentRevision() == revision &&
                   _session.history.CanRedo() && !_session.history.IsModified(),
               "invalid-dimensions-preserve-history-redo-and-saved-revision");
        expect(_session.PendingTransformEdit() && _session.PendingTransformEdit()->opening == opening &&
                   _session.TransformDraftRevision() == draftRevision &&
                   _session.PendingTransformEdit()->draft.origin == Vec2{3, 4},
               "invalid-dimensions-never-discard-the-live-deferred-draft");
        expect(_sizeDialog.kind == SizeCardKind::NewCanvas && _documentPath == L"invalid-lifecycle-source.prism" &&
                   _errorsShown == notices,
               "invalid-dimensions-keep-card-path-and-validation-state");
        const auto cancel = QueueHostInput(HostTransformFinishInput{active, draftRevision, true});
        if (!settle() || cancel.Read().outcome != HostOutcome::Succeeded)
            return 5;
        CancelSizeCard();
    }
    // Values are rounded in floating point, range-checked, then converted. These inputs independently name
    // the exact boundary outputs without invoking a replacement or relying on a GUI field parser.
    for (const auto item : std::array<std::array<double, 4>, 4>{std::array<double, 4>{0.5, 1.49, 1, 1},
                                                                {30000.49, 30000.49, 30000, 30000},
                                                                {5.5, 7.49, 6, 7},
                                                                {5.49, 7.5, 5, 8}})
    {
        HostSizeDraft draft;
        draft.newWidth = item[0];
        draft.newHeight = item[1];
        const auto dimensions = NewCanvasDimensions(draft);
        expect(dimensions && (*dimensions)[0] == item[2] && (*dimensions)[1] == item[3],
               "finite-rounding-boundaries-have-independent-literal-dimensions");
    }
    std::printf("document-lifecycle-refusal summary: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
