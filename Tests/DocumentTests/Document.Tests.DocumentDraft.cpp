#include "Document.Tests.DocumentDraft.h"

#include "../../Common/DocumentDraft.h"

#include <array>
#include <cstdio>
#include <limits>
#include <span>
#include <type_traits>
#include <utility>

namespace
{
int gChecks = 0;
int gFailures = 0;

void Expect(bool condition, const char* name) noexcept
{
    ++gChecks;
    std::printf("%s document-draft.%s\n", condition ? "PASS" : "FAIL", name);
    if (!condition)
        ++gFailures;
}

constexpr std::array<std::uint8_t, 16> kDocumentId{0x10, 0x21, 0x32, 0x43, 0x54, 0x65, 0x76, 0x87,
                                                   0x98, 0xa9, 0xba, 0xcb, 0xdc, 0xed, 0xfe, 0x0f};
constexpr std::array<std::uint8_t, 16> kLayerAId{0x01, 0x12, 0x23, 0x34, 0x45, 0x56, 0x67, 0x78,
                                                 0x89, 0x9a, 0xab, 0xbc, 0xcd, 0xde, 0xef, 0x10};
constexpr std::array<std::uint8_t, 16> kLayerBId{0x02, 0x13, 0x24, 0x35, 0x46, 0x57, 0x68, 0x79,
                                                 0x8a, 0x9b, 0xac, 0xbd, 0xce, 0xdf, 0xf0, 0x11};
constexpr std::array<std::uint8_t, 16> kLayerCId{0x03, 0x14, 0x25, 0x36, 0x47, 0x58, 0x69, 0x7a,
                                                 0x8b, 0x9c, 0xad, 0xbe, 0xcf, 0xe0, 0xf1, 0x12};
constexpr std::array<std::uint8_t, 16> kLayerDId{0x04, 0x15, 0x26, 0x37, 0x48, 0x59, 0x6a, 0x7b,
                                                 0x8c, 0x9d, 0xae, 0xbf, 0xd0, 0xe1, 0xf2, 0x13};
constexpr std::array<std::uint8_t, 16> kLayerXId{0x05, 0x16, 0x27, 0x38, 0x49, 0x5a, 0x6b, 0x7c,
                                                 0x8d, 0x9e, 0xaf, 0xc0, 0xd1, 0xe2, 0xf3, 0x14};

using DocumentLayers = decltype(CanvasDocument::layers);
static_assert(std::is_same_v<DocumentLayers, RecordRoot<ImageLayer>>);

ImageLayer Layer(const std::array<std::uint8_t, 16>& id, const wchar_t* name)
{
    ImageLayer result;
    result.id.bytes = id;
    result.name = name;
    return result;
}

CanvasDocument SourceDocument()
{
    CanvasDocument result;
    result.id.bytes = kDocumentId;
    result.width = 640;
    result.height = 360;
    result.resolution = 144.5;

    ImageLayer first = Layer(kLayerAId, L"underpaint");
    first.asset.emplace(); // Both rasters remain default-constructed and empty.
    first.asset.value().name = L"empty-asset";
    first.visible = false;
    first.isGroup = true;
    first.opacity = 0.625;
    first.blendMode = LayerBlendMode::Multiply;

    ImageLayer second = Layer(kLayerBId, L"ink");
    second.parentID = first.id;
    second.maskSourceID = first.id;
    second.opacity = 0.75;
    second.blendMode = LayerBlendMode::Screen;

    ImageLayer third = Layer(kLayerCId, L"paper");
    third.visible = true;
    third.transform.origin = Vec2{7.25, 9.5};
    third.transform.rotation = 0.125;
    third.transform.flipX = true;
    third.transform.sampling = LayerSampling::Nearest;

    RecordRoot<ImageLayer>::Builder builder(3);
    static_cast<void>(builder.Append(std::move(first)));
    static_cast<void>(builder.Append(std::move(second)));
    static_cast<void>(builder.Append(std::move(third)));
    auto root = builder.Seal();
    result.layers = std::move(root.value());

    result.selection.emplace();
    result.selection.value().antialiased = false;
    result.selection.value().loops = SelectionLoops{{Vec2{2, 3}, Vec2{12, 3}, Vec2{12, 8}, Vec2{2, 8}}};
    return result;
}

bool HasDocumentScalars(const CanvasDocument& document, int width, int height, double resolution, bool hasSelection,
                        bool antialiased)
{
    if (document.id.bytes != kDocumentId || document.width != width || document.height != height ||
        document.resolution != resolution || document.selection.has_value() != hasSelection)
        return false;
    if (!hasSelection)
        return true;
    const DocumentSelection& selection = document.selection.value();
    return selection.antialiased == antialiased;
}

bool HasSourceLayerA(const ImageLayer& layer)
{
    return layer.id.bytes == kLayerAId && layer.name == L"underpaint" && layer.asset.has_value() &&
           layer.asset.value().name == L"empty-asset" && !layer.visible && layer.isGroup && layer.opacity == 0.625 &&
           layer.blendMode == LayerBlendMode::Multiply && !layer.parentID.has_value() &&
           !layer.maskSourceID.has_value();
}

bool HasSourceLayerB(const ImageLayer& layer)
{
    return layer.id.bytes == kLayerBId && layer.name == L"ink" && layer.visible && !layer.isGroup &&
           layer.opacity == 0.75 && layer.blendMode == LayerBlendMode::Screen && layer.parentID.has_value() &&
           layer.parentID.value().bytes == kLayerAId && layer.maskSourceID.has_value() &&
           layer.maskSourceID.value().bytes == kLayerAId;
}

bool HasSourceLayerC(const ImageLayer& layer)
{
    return layer.id.bytes == kLayerCId && layer.name == L"paper" && layer.visible && !layer.isGroup &&
           layer.opacity == 1 && layer.blendMode == LayerBlendMode::Normal && layer.transform.origin.x == 7.25 &&
           layer.transform.origin.y == 9.5 && layer.transform.rotation == 0.125 && layer.transform.flipX &&
           !layer.transform.flipY && layer.transform.sampling == LayerSampling::Nearest;
}

bool HasSelection(const CanvasDocument& document, bool antialiased, double x0, double y0, double x1, double y1)
{
    if (!document.selection.has_value())
        return false;
    const DocumentSelection& selection = document.selection.value();
    if (selection.antialiased != antialiased || selection.loops.size() != 1 || selection.loops[0].size() != 4)
        return false;
    const SelectionLoop& loop = selection.loops[0];
    return loop[0].x == x0 && loop[0].y == y0 && loop[1].x == x1 && loop[1].y == y0 && loop[2].x == x1 &&
           loop[2].y == y1 && loop[3].x == x0 && loop[3].y == y1;
}
} // namespace

int RunDocumentDraftTests()
{
    gChecks = 0;
    gFailures = 0;

    {
        CanvasDocument source = SourceDocument();
        const RecordRoot<ImageLayer> captured = source.layers;
        const void* const capturedIdentity = captured.Identity();
        auto draft = CanvasDocumentDraft::Clone(source, 5);
        const bool cloneReady = draft.has_value();
        bool initialClone = false;
        if (cloneReady)
        {
            const CanvasDocument& state = draft.value().DocumentState();
            const std::span<const ImageLayer> records = draft.value().ReadLayers();
            initialClone = draft.value().IsEditable() && state.layers.Identity() == capturedIdentity &&
                           records.size() == 3 && records.data() != captured.data() &&
                           HasDocumentScalars(state, 640, 360, 144.5, true, false) &&
                           HasSelection(state, false, 2, 3, 12, 8) && HasSourceLayerA(records[0]) &&
                           HasSourceLayerB(records[1]) && HasSourceLayerC(records[2]);
        }
        Expect(cloneReady && initialClone, "clone-retains-shared-root-and-explicitly-copies-records-and-scalars");

        if (cloneReady)
        {
            CanvasDocumentDraft& edit = draft.value();
            edit.DocumentState().width = 800;
            edit.DocumentState().height = 450;
            edit.DocumentState().resolution = 96.25;
            edit.DocumentState().selection.value().antialiased = true;
            edit.DocumentState().selection.value().loops =
                SelectionLoops{{Vec2{4, 5}, Vec2{24, 5}, Vec2{24, 15}, Vec2{4, 15}}};
            {
                std::span<ImageLayer> mutableRecords = edit.EditLayers();
                mutableRecords[0].name = L"edited-underpaint";
                mutableRecords[0].opacity = 0.375;
            }

            ImageLayer inserted = Layer(kLayerXId, L"inserted-between");
            inserted.visible = false;
            const bool insertedOkay = edit.Insert(1, std::move(inserted));
            bool appendedOkay = false;
            {
                ImageLayer temporary = Layer(kLayerDId, L"appended-from-short-lived-input");
                appendedOkay = edit.Append(std::move(temporary));
            }
            const bool erasedOkay = edit.Erase(3);
            const std::span<const ImageLayer> expected = edit.ReadLayers();
            const bool orderAndMetadata =
                expected.size() == 4 && expected[0].id.bytes == kLayerAId && expected[0].name == L"edited-underpaint" &&
                expected[0].opacity == 0.375 && expected[1].id.bytes == kLayerXId &&
                expected[1].name == L"inserted-between" && !expected[1].visible && expected[2].id.bytes == kLayerBId &&
                HasSourceLayerB(expected[2]) && expected[3].id.bytes == kLayerDId &&
                expected[3].name == L"appended-from-short-lived-input";
            const bool scalarEdits = HasDocumentScalars(edit.DocumentState(), 800, 450, 96.25, true, true) &&
                                     HasSelection(edit.DocumentState(), true, 4, 5, 24, 15);
            auto published = edit.Seal();
            const bool publishedValues =
                published.has_value() && published.value().layers.Identity() != capturedIdentity &&
                published.value().layers.size() == 4 && published.value().layers[0].id.bytes == kLayerAId &&
                published.value().layers[0].name == L"edited-underpaint" &&
                published.value().layers[0].opacity == 0.375 && published.value().layers[1].id.bytes == kLayerXId &&
                published.value().layers[2].id.bytes == kLayerBId &&
                published.value().layers[3].id.bytes == kLayerDId &&
                HasDocumentScalars(published.value(), 800, 450, 96.25, true, true) &&
                HasSelection(published.value(), true, 4, 5, 24, 15);
            Expect(insertedOkay && appendedOkay && erasedOkay && orderAndMetadata && scalarEdits && publishedValues,
                   "edit-span-insert-erase-append-seal-preserve-literal-order-and-scalar-edits");
            Expect(source.layers.Identity() == capturedIdentity && source.layers.size() == 3 &&
                       HasSourceLayerA(source.layers[0]) && HasSourceLayerB(source.layers[1]) &&
                       HasSourceLayerC(source.layers[2]) && captured.Identity() == capturedIdentity,
                   "private-clone-edits-leave-source-and-captured-root-order-names-and-metadata-unchanged");

            source = CanvasDocument{};
            draft.reset();
            Expect(captured.size() == 3 && HasSourceLayerA(captured[0]) && HasSourceLayerB(captured[1]) &&
                       HasSourceLayerC(captured[2]),
                   "captured-root-keeps-source-records-alive-after-source-and-draft-destruction");
        }
    }

    {
        CanvasDocument state = SourceDocument();
        auto rebuilt = CanvasDocumentDraft::Rebuild(std::move(state), 1);
        const bool ready = rebuilt.has_value();
        bool refusalBeforeMove = false;
        if (ready)
        {
            ImageLayer accepted = Layer(kLayerAId, L"accepted");
            ImageLayer rejectedAppend = Layer(kLayerBId, L"append-must-stay-owned-by-caller");
            ImageLayer rejectedFullInsert = Layer(kLayerCId, L"insert-must-stay-owned-by-caller");
            ImageLayer rejectedIndex = Layer(kLayerDId, L"bad-index-must-stay-owned-by-caller");
            const bool acceptedOkay = rebuilt.value().Append(std::move(accepted));
            const bool appendRefused = !rebuilt.value().Append(std::move(rejectedAppend));
            const bool fullInsertRefused = !rebuilt.value().Insert(0, std::move(rejectedFullInsert));
            const bool indexRefused = !rebuilt.value().Insert(2, std::move(rejectedIndex));
            refusalBeforeMove =
                acceptedOkay && appendRefused && fullInsertRefused && indexRefused &&
                rejectedAppend.id.bytes == kLayerBId && rejectedAppend.name == L"append-must-stay-owned-by-caller" &&
                rejectedFullInsert.id.bytes == kLayerCId &&
                rejectedFullInsert.name == L"insert-must-stay-owned-by-caller" && rejectedIndex.id.bytes == kLayerDId &&
                rejectedIndex.name == L"bad-index-must-stay-owned-by-caller" && !rebuilt.value().Erase(1) &&
                rebuilt.value().ReadLayers().size() == 1;
        }
        Expect(ready && refusalBeforeMove, "capacity-and-index-refusal-happen-before-rvalue-input-move");
    }

    {
        CanvasDocument source = SourceDocument();
        const RecordRoot<ImageLayer> original = source.layers;
        CancelSource canceledCloneSource;
        static_cast<void>(canceledCloneSource.Cancel());
        auto canceledClone = CanvasDocumentDraft::Clone(source, 3, canceledCloneSource.Token());
        CancelSource canceledRebuildSource;
        static_cast<void>(canceledRebuildSource.Cancel());
        CanvasDocument canceledInput = source;
        auto canceledRebuild = CanvasDocumentDraft::Rebuild(std::move(canceledInput), 3, canceledRebuildSource.Token());

        auto draft = CanvasDocumentDraft::Clone(source, 4);
        CancelSource canceledSealSource;
        if (draft.has_value())
            static_cast<void>(draft.value().Append(Layer(kLayerDId, L"before-canceled-seal")));
        static_cast<void>(canceledSealSource.Cancel());
        auto canceledSeal = draft.has_value() ? draft.value().Seal(canceledSealSource.Token()) : std::nullopt;
        const bool retryStillEditable = draft.has_value() && draft.value().IsEditable() &&
                                        draft.value().ReadLayers().size() == 4 &&
                                        draft.value().DocumentState().layers.Identity() == original.Identity();
        auto sealedAfterCancel = draft.has_value() ? draft.value().Seal() : std::nullopt;
        const bool originalPreserved = source.layers.Identity() == original.Identity() && source.layers.size() == 3 &&
                                       HasSourceLayerA(source.layers[0]) && HasSourceLayerB(source.layers[1]) &&
                                       HasSourceLayerC(source.layers[2]);
        Expect(!canceledClone.has_value() && !canceledRebuild.has_value() &&
                   canceledInput.layers.Identity() == original.Identity() &&
                   HasDocumentScalars(canceledInput, 640, 360, 144.5, true, false) && !canceledSeal.has_value() &&
                   retryStillEditable && sealedAfterCancel.has_value() && originalPreserved,
               "canceled-clone-rebuild-and-seal-refuse-with-original-root-preserved-and-seal-retryable");
    }

    {
        CanvasDocument source = SourceDocument();
        auto initial = CanvasDocumentDraft::Clone(source, 4);
        bool movesWork = false;
        if (initial.has_value())
        {
            CanvasDocumentDraft moved(std::move(initial.value()));
            const bool moveSourceConsumed = !initial.value().IsEditable() && initial.value().EditLayers().empty() &&
                                            !initial.value().Append(Layer(kLayerDId, L"refused-after-move"));
            CanvasDocumentDraft assigned = CanvasDocumentDraft::Rebuild(CanvasDocument{}, 0).value();
            assigned = std::move(moved);
            const bool assignmentSourceConsumed = !moved.IsEditable() && moved.EditLayers().empty() &&
                                                  !moved.Append(Layer(kLayerDId, L"refused-after-assignment"));
            auto sealed = assigned.Seal();
            const bool sealedStateConsumed = !assigned.IsEditable() && assigned.ReadLayers().empty() &&
                                             assigned.EditLayers().empty() &&
                                             !assigned.Append(Layer(kLayerDId, L"refused-after-seal")) &&
                                             !assigned.Insert(0, Layer(kLayerDId, L"refused-after-seal")) &&
                                             !assigned.Erase(0) && !assigned.Seal().has_value();
            movesWork = moveSourceConsumed && assignmentSourceConsumed && sealed.has_value() &&
                        sealed.value().layers.size() == 3 && HasSourceLayerA(sealed.value().layers[0]) &&
                        HasSourceLayerB(sealed.value().layers[1]) && HasSourceLayerC(sealed.value().layers[2]) &&
                        sealedStateConsumed;
        }
        Expect(movesWork, "move-construction-assignment-and-seal-consume-sources-and-reject-later-edits");
    }

    {
        CanvasDocument empty;
        empty.id.bytes = kDocumentId;
        empty.width = 19;
        empty.height = 23;
        empty.resolution = 300;
        auto zeroCapacity = CanvasDocumentDraft::Rebuild(empty, 0);
        auto emptySealed = zeroCapacity.has_value() ? zeroCapacity.value().Seal() : std::nullopt;
        auto tooSmallClone = CanvasDocumentDraft::Clone(SourceDocument(), 2);
        auto tooLargeRebuild =
            CanvasDocumentDraft::Rebuild(CanvasDocument{}, static_cast<std::size_t>(kMaxLayerCount) + 1);
        auto impossibleEmpty =
            CanvasDocumentDraft::Rebuild(CanvasDocument{}, (std::numeric_limits<std::size_t>::max)());
        auto optionalDraft = CanvasDocumentDraft::Rebuild(SourceDocument(), 3);
        if (optionalDraft.has_value())
            optionalDraft.value().DocumentState().selection.reset();
        auto noSelection = optionalDraft.has_value() ? optionalDraft.value().Seal() : std::nullopt;
        Expect(emptySealed.has_value() && emptySealed.value().layers.empty() &&
                   emptySealed.value().id.bytes == kDocumentId && emptySealed.value().width == 19 &&
                   emptySealed.value().height == 23 && emptySealed.value().resolution == 300 &&
                   !tooSmallClone.has_value() && !tooLargeRebuild.has_value() && !impossibleEmpty.has_value() &&
                   noSelection.has_value() && noSelection.value().id.bytes == kDocumentId &&
                   noSelection.value().width == 640 && noSelection.value().height == 360 &&
                   noSelection.value().resolution == 144.5 && !noSelection.value().selection.has_value(),
               "empty-zero-capacity-seals-and-undersized-or-oversized-limits-refuse");
    }

    std::printf("SUMMARY document-draft checks=%d failures=%d\n", gChecks, gFailures);
    return gFailures;
}
