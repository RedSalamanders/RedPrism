#include "Application.h"

#include "Tests/Support/Support.Tests.DocumentFixture.h"

#include "ChromeHost.h"
#include "Selection.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <utility>

int Application::RunTransformPolicySelfTest() noexcept
{
    const bool savedAcceleratorShift = _acceleratorShift;
    const auto restoreAcceleratorShift = wil::scope_exit([&]() noexcept { _acceleratorShift = savedAcceleratorShift; });
    constexpr std::array<std::uint8_t, 16> sourceBytes{11, 21, 31, 255, 41,  51,  61,  255,
                                                       71, 81, 91, 255, 101, 111, 121, 255};
    const Raster source = Raster::Make(2, 2, RasterFormat::Bgra8888, sourceBytes.data(), sourceBytes.size());
    if (!source.HasStorage())
        return 5;

    unsigned checks = 0;
    unsigned failures = 0;
    const auto expect = [&](bool condition, const char* name) noexcept
    {
        ++checks;
        failures += condition ? 0u : 1u;
        std::printf("%s transform-policy-host.%s\n", condition ? "PASS" : "FAIL", name);
        std::fflush(stdout);
    };
    const auto settle = [&]() noexcept
    { return SettleJobs(_jobs, 15000, DispatchMessageThunk, this) == SettleResult::Settled; };
    const auto terminal = [&]() noexcept
    {
        return !_activeHostRequest && !_hostRequests.Size() && !_transformWorkTicket && !_transformPreviewTicket &&
               !_historyWork.Busy() && !_hostCommitStamp && !_hostInputBudget.Used() && !_jobs.ReservedMemoryBytes() &&
               !_jobs.ReleasingMemoryBytes();
    };
    const auto escapeAndSettle = [&]() noexcept
    {
        _chrome->SetCanvasFocused(true);
        (void)HandleAccelerator(VK_ESCAPE, 0);
        return settle();
    };
    const auto prepare = [&](bool withSelection = false) noexcept -> Guid
    {
        if (!settle() || !escapeAndSettle())
            return {};

        DocumentFixture document;
        document.id = Guid::Generate();
        document.width = 16;
        document.height = 16;
        if (withSelection)
            document.selection = DocumentSelection{true, SelectionGeometry::Rect(2, 2, 2, 2)};
        ImageLayer layer = ImageLayer::Blank(L"Policy source", Vec2{2.0, 2.0});
        layer.asset = RasterAsset{source, source, layer.name};
        layer.transform.origin = Vec2{2.0, 2.0};
        layer.transform.sampling = LayerSampling::Nearest;
        const Guid target = layer.id;
        document.layers.push_back(std::move(layer));
        _session.ReplaceDocument(std::move(document).Seal(), target);
        _session.history.Reset();
        _session.history.MarkSaved();
        _viewport.SetZoom(1.0, _viewport.Center(), DocumentSize());
        DocumentChanged();
        return _session.ActiveLayer() && _session.ActiveLayer()->asset &&
                       _session.ActiveLayer()->asset->image == source &&
                       _session.Document()->selection.has_value() == withSelection
                   ? target
                   : Guid{};
    };

    // Free Transform on a layer with no selection opens a persistent deferred draft.
    {
        const Guid target = prepare();
        if (target == Guid{})
            return 5;
        OnCommand(ChromeHost::kCommandFreeTransform);
        if (!settle())
            return 5;
        const auto& pending = _session.PendingTransformEdit();
        expect(pending && pending->layerID == target && pending->persistent && pending->deferred,
               "free-transform-opens-deferred-persistent-layer-edit");
        expect(_session.ActiveLayer()->transform.origin == Vec2{2.0, 2.0} &&
                   _session.ActiveLayer()->asset->image == source && _session.history.UndoCount() == 0,
               "free-transform-keeps-source-pixels-transform-and-history");
        expect(terminal(), "free-transform-retires-host-work");
        if (!escapeAndSettle())
            return 5;
        expect(!_session.PendingTransformEdit() && terminal(), "free-transform-escape-acknowledges-cancellation");
    }

    // A typed header edit starts a persistent deferred draft; Apply publishes the exact typed origin once.
    {
        const Guid target = prepare();
        if (target == Guid{})
            return 5;
        OnTransformAction(TransformAction::SetX, 4.0);
        const auto& pending = _session.PendingTransformEdit();
        expect(pending && pending->layerID == target && pending->persistent && pending->deferred &&
                   pending->draft.origin == Vec2{4.0, 2.0},
               "typed-x-opens-deferred-persistent-draft-at-four");
        expect(_session.ActiveLayer()->transform.origin == Vec2{2.0, 2.0} &&
                   _session.ActiveLayer()->asset->image == source && _session.history.UndoCount() == 0,
               "typed-x-does-not-publish-source-transform-or-pixels");
        OnTransformAction(TransformAction::Apply, 0.0);
        if (!settle())
            return 5;
        const ImageLayer* layer = _session.ActiveLayer();
        expect(!_session.PendingTransformEdit() && layer && layer->transform.origin == Vec2{4.0, 2.0},
               "typed-x-apply-commits-exact-origin-four");
        expect(layer && layer->asset && layer->asset->image == source && _session.history.UndoCount() == 1 &&
                   _session.history.UndoName() == L"Transform Layer",
               "typed-x-apply-retains-source-and-records-one-transform-layer-entry");
        expect(terminal(), "typed-x-apply-retires-all-host-work");
    }

    // A Ctrl body drag bypasses snapping, opens an ephemeral deferred edit and commits its literal displacement.
    {
        const Guid target = prepare();
        if (target == Guid{})
            return 5;
        const Vec2 start = _viewport.ViewFromDocument(Vec2{3.0, 3.0}, DocumentSize());
        const Vec2 finish = _viewport.ViewFromDocument(Vec2{5.0, 4.0}, DocumentSize());
        _overlayGeometry.reset(); // The 2x2 box's handles overlap this path at zoom 1; exercise the body hit path.
        BeginTransformDrag(start, MK_CONTROL);
        const auto& opened = _session.PendingTransformEdit();
        expect(opened && opened->layerID == target && !opened->persistent && opened->deferred,
               "body-drag-opens-deferred-ephemeral-edit");
        ContinueTransformDrag(finish, MK_CONTROL);
        const auto& moved = _session.PendingTransformEdit();
        expect(moved && moved->draft.origin == Vec2{4.0, 3.0} &&
                   _session.ActiveLayer()->transform.origin == Vec2{2.0, 2.0} &&
                   _session.ActiveLayer()->asset->image == source,
               "ctrl-body-drag-keeps-literal-two-by-one-draft-with-source-unchanged");
        EndTransformDrag();
        if (!settle())
            return 5;
        const ImageLayer* layer = _session.ActiveLayer();
        expect(!_session.PendingTransformEdit() && layer && layer->transform.origin == Vec2{4.0, 3.0},
               "body-drag-settles-exact-two-by-one-displacement");
        expect(layer && layer->asset && layer->asset->image == source && _session.history.UndoCount() == 1 &&
                   _session.history.UndoName() == L"Transform Layer",
               "body-drag-retains-source-and-records-one-transform-layer-entry");
        expect(terminal(), "body-drag-retires-all-host-work");
    }

    // The real layer nudge command queues its one-pixel offset through the deferred transform commit.
    {
        const Guid target = prepare();
        if (target == Guid{})
            return 5;
        _acceleratorShift = false;
        OnCommand(ChromeHost::kCommandNudgeLayerRight);
        if (!settle())
            return 5;
        const ImageLayer* layer = _session.ActiveLayer();
        expect(!_session.PendingTransformEdit() && layer && layer->transform.origin == Vec2{3.0, 2.0},
               "right-nudge-commits-exact-one-pixel-translation");
        expect(layer && layer->asset && layer->asset->image == source && _session.history.UndoCount() == 1 &&
                   _session.history.UndoName() == L"Transform Layer",
               "right-nudge-records-one-transform-layer-entry-and-retains-source");
        expect(terminal(), "right-nudge-retires-all-host-work");
    }

    // A rectangular selection takes the real floating-opening worker, then publishes a deferred floating draft.
    {
        const Guid target = prepare(true);
        if (target == Guid{})
            return 5;
        OnCommand(ChromeHost::kCommandFreeTransform);
        if (!settle())
            return 5;
        const auto& pending = _session.PendingTransformEdit();
        expect(pending && pending->floating && pending->deferred,
               "rectangular-selection-free-transform-opens-deferred-floating-edit");
        expect(_session.Document()->layers.size() == 2 && _session.history.UndoCount() == 0 && _transformRestoreOwner &&
                   terminal(),
               "floating-open-retains-rollback-owner-without-history-or-live-work");
        if (!escapeAndSettle())
            return 5;
        expect(!_session.PendingTransformEdit() && _session.Document()->layers.size() == 1 &&
                   _session.ActiveLayer()->asset && _session.ActiveLayer()->asset->image == source &&
                   _session.history.UndoCount() == 0 && !_transformRestoreOwner && terminal(),
               "floating-open-escape-restores-source-and-retires-owner");
    }

    // Losing a drag-only pointer must cancel through the worker closure and keep the original image/history.
    {
        if (prepare() == Guid{})
            return 5;
        const auto generation = _session.Generation();
        _overlayGeometry.reset();
        BeginTransformDrag(_viewport.ViewFromDocument(Vec2{3, 3}, DocumentSize()), MK_CONTROL);
        ContinueTransformDrag(_viewport.ViewFromDocument(Vec2{5, 4}, DocumentSize()), MK_CONTROL);
        expect(_session.PendingTransformEdit() && _session.PendingTransformEdit()->deferred &&
                   !_session.PendingTransformEdit()->persistent &&
                   _session.PendingTransformEdit()->draft.origin == Vec2{4, 3},
               "lost-ephemeral-pointer-starts-with-exact-deferred-draft");
        EndPointerDrag(PointerEnd::Lost);
        if (!settle())
            return 5;
        expect(!_session.PendingTransformEdit() && _session.ActiveLayer()->transform.origin == Vec2{2, 2} &&
                   _session.ActiveLayer()->asset->image == source && _session.Generation() == generation &&
                   _session.history.UndoCount() == 0 && terminal(),
               "lost-ephemeral-pointer-acknowledges-without-source-or-history-mutation");
    }

    // Losing a pointer in a persistent card rewinds only that drag, retaining the earlier typed draft.
    {
        if (prepare() == Guid{})
            return 5;
        OnTransformAction(TransformAction::SetX, 4);
        if (!settle())
            return 5;
        _overlayGeometry.reset();
        BeginTransformDrag(_viewport.ViewFromDocument(Vec2{5, 3}, DocumentSize()), MK_CONTROL);
        ContinueTransformDrag(_viewport.ViewFromDocument(Vec2{7, 4}, DocumentSize()), MK_CONTROL);
        expect(_session.PendingTransformEdit() && _session.PendingTransformEdit()->persistent &&
                   _session.PendingTransformEdit()->draft.origin == Vec2{6, 3},
               "lost-persistent-pointer-starts-with-exact-later-drag");
        EndPointerDrag(PointerEnd::Lost);
        if (!settle())
            return 5;
        expect(_session.PendingTransformEdit() && _session.PendingTransformEdit()->deferred &&
                   _session.PendingTransformEdit()->persistent &&
                   _session.PendingTransformEdit()->draft.origin == Vec2{4, 2} &&
                   _session.ActiveLayer()->transform.origin == Vec2{2, 2} && _session.history.UndoCount() == 0,
               "lost-persistent-pointer-retains-typed-opening-and-rewinds-only-drag");
        OnTransformAction(TransformAction::Apply, 0);
        if (!settle())
            return 5;
        expect(!_session.PendingTransformEdit() && _session.ActiveLayer()->transform.origin == Vec2{4, 2} &&
                   _session.ActiveLayer()->asset->image == source && _session.history.UndoCount() == 1 && terminal(),
               "lost-persistent-pointer-apply-commits-only-retained-typed-origin");
    }

    // Ctrl on a real corner enters distortion while retaining the immutable committed source.
    {
        const Guid target = prepare();
        if (target == Guid{})
            return 5;
        _viewport.SetZoom(32, _viewport.Center(), DocumentSize());
        _overlayGeometry =
            TransformOverlayGeometry::FromTransform(_session.ActiveLayer()->transform, _viewport, DocumentSize());
        BeginTransformDrag(_overlayGeometry->handles[0], MK_CONTROL);
        ContinueTransformDrag(_viewport.ViewFromDocument(Vec2{1, 2}, DocumentSize()), MK_CONTROL);
        const auto& pending = _session.PendingTransformEdit();
        expect(pending && pending->layerID == target && pending->deferred && pending->persistent && pending->corners &&
                   pending->corners->at(0) == Vec2{1, 2} && pending->corners->at(1) == Vec2{4, 2} &&
                   pending->corners->at(2) == Vec2{4, 4} && pending->corners->at(3) == Vec2{2, 4},
               "ctrl-corner-opens-deferred-distortion-at-independent-literal-corners");
        EndTransformDrag();
        if (!settle())
            return 5;
        expect(_session.PendingTransformEdit() && _session.ActiveLayer()->asset->image == source &&
                   _session.ActiveLayer()->transform.origin == Vec2{2, 2} && _session.history.UndoCount() == 0,
               "ctrl-corner-release-keeps-persistent-draft-and-committed-source");
        OnTransformAction(TransformAction::Apply, 0);
        if (!settle())
            return 5;
        const auto* layer = _session.ActiveLayer();
        expect(!_session.PendingTransformEdit() && layer && layer->transform.origin == Vec2{1, 2} &&
                   layer->transform.size == Vec2{3, 2} && layer->asset && layer->asset->image != source &&
                   layer->asset->image.Width() == 3 && layer->asset->image.Height() == 2 &&
                   _session.history.UndoCount() == 1 && terminal(),
               "ctrl-corner-apply-publishes-worker-warp-with-exact-three-by-two-bounds");
        OnCommand(ChromeHost::kCommandUndo);
        if (!settle())
            return 5;
        expect(
            _session.ActiveLayer()->asset->image == source && _session.ActiveLayer()->transform.origin == Vec2{2, 2} &&
                _session.ActiveLayer()->transform.size == Vec2{2, 2} && _session.history.UndoCount() == 0 && terminal(),
            "ctrl-corner-undo-restores-exact-source-pixels-and-placement");
        _viewport.SetZoom(1, _viewport.Center(), DocumentSize());
    }

    if (!escapeAndSettle())
        return 5;
    std::printf("transform-policy-host summary: %u checks, %u failures\n", checks, failures);
    std::fflush(stdout);
    return failures ? 5 : 0;
}
