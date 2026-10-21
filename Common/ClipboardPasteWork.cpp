#include "ClipboardPasteWork.h"

#include "DocumentDraft.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <unordered_set>
#include <utility>

namespace
{
bool AddPasteBytes(std::uint64_t& total, std::uint64_t amount) noexcept
{
    if (!amount || amount > (std::numeric_limits<std::uint64_t>::max)() - total)
        return false;
    total += amount;
    return true;
}
} // namespace

std::int64_t DocumentSession::ClipboardPastePixelLimit() const noexcept
{
    if (!_document || _transformEdit || _gradient || _pixelMove || _document->layers.size() >= kMaxLayerCount)
        return 0;
    return PixelBudgetLeft(false);
}

std::optional<ClipboardPasteInput> DocumentSession::PrepareClipboardPasteInput() const
{
    if (ClipboardPastePixelLimit() <= 0)
        return std::nullopt;
    return ClipboardPasteInput(CaptureImageImportAuthority(), *_document, _activeLayerID, history.CaptureState(),
                               _clipboard, _clipboardSerial);
}

std::optional<PreparedClipboardPaste> DocumentSession::PrepareClipboardPastePublication(
    ClipboardPasteInput input, bool privateOrigin, const Raster& foreignImage, CancelToken cancellation,
    DocumentHistory::PrepareObserver observer)
{
    if (!input._valid || cancellation.IsCancelled() || input._source.layers.size() >= kMaxLayerCount ||
        (privateOrigin && !input._clipboard))
        return std::nullopt;
    const Raster image = privateOrigin ? input._clipboard->image : foreignImage;
    if (!image.HasStorage())
        return std::nullopt;
    std::int64_t usedPixels = 0;
    std::unordered_set<std::wstring> names;
    names.reserve(input._source.layers.size());
    std::optional<std::size_t> activeIndex;
    for (std::size_t i = 0; i < input._source.layers.size(); ++i)
    {
        if (cancellation.IsCancelled())
            return std::nullopt;
        const auto& layer = input._source.layers[i];
        names.insert(layer.name);
        if (layer.asset)
        {
            usedPixels += layer.asset->image.PixelCount();
            if (usedPixels > kMaxLayerPixels)
                return std::nullopt;
        }
        if (input._active == layer.id)
            activeIndex = i;
    }
    if (image.PixelCount() > kMaxLayerPixels - usedPixels || (input._active && !activeIndex))
        return std::nullopt;
    std::wstring name;
    for (std::size_t number = 1; number <= input._source.layers.size() + 1; ++number)
    {
        if (cancellation.IsCancelled())
            return std::nullopt;
        name = L"Layer " + std::to_wstring(number);
        if (!names.contains(name))
            break;
    }
    const Vec2 origin = privateOrigin ? input._clipboard->origin
                                      : Vec2{std::floor((input._source.width - image.Width()) / 2.0),
                                             std::floor((input._source.height - image.Height()) / 2.0)};
    ImageLayer added =
        ImageLayer::Blank(name, Vec2{static_cast<double>(image.Width()), static_cast<double>(image.Height())});
    added.asset = RasterAsset{image, {}, std::move(name)};
    added.transform.origin = origin;
    std::size_t insertion = input._source.layers.size();
    if (activeIndex)
    {
        const auto& active = input._source.layers[activeIndex.value()];
        added.parentID = active.isGroup ? input._active : active.parentID;
        insertion = activeIndex.value() + 1;
        if (active.isGroup)
        {
            // Same hierarchy contract as InsertLayerAboveActive: append inside the group's whole subtree.
            // This allocation and traversal belong to the worker, never to owner publication.
            const auto descendants = DescendantIds(input._source.layers, active.id);
            for (std::size_t i = 0; i < input._source.layers.size(); ++i)
            {
                if (cancellation.IsCancelled())
                    return std::nullopt;
                if (descendants.contains(input._source.layers[i].id))
                    insertion = (std::max)(insertion, i + 1);
            }
        }
    }
    const auto active = added.id;
    const auto parent = added.parentID;
    auto draft = CanvasDocumentDraft::Clone(input._source, input._source.layers.size() + 1, cancellation);
    if (!draft)
        return std::nullopt;
    if (cancellation.IsCancelled() || !draft.value().Insert(insertion, std::move(added)))
        return std::nullopt;
    draft.value().DocumentState().selection.reset();
    auto sealed = draft.value().Seal(cancellation);
    if (!sealed || cancellation.IsCancelled())
        return std::nullopt;
    ImageImportBatchResult imported;
    imported.status = ProjectIoStatus::Ok;
    imported.importedDocument = std::move(sealed.value());
    imported.importedActiveLayerID = active;
    imported.historyName = L"Paste";
    auto publication =
        PrepareImageImportPublication(std::move(input._authority), std::move(input._source), input._active,
                                      std::move(input._history), std::move(imported), cancellation, observer);
    if (!publication || cancellation.IsCancelled())
        return std::nullopt;
    return PreparedClipboardPaste(std::move(publication.value()), parent,
                                  privateOrigin ? std::optional(input._serial) : std::nullopt);
}

bool DocumentSession::ApplyPreparedClipboardPasteIfCurrent(PreparedClipboardPaste& prepared) noexcept
{
    if ((prepared._privateSerial && _clipboardSerial != prepared._privateSerial.value()) ||
        !ApplyPreparedImageImportIfCurrent(prepared._publication))
        return false;
    // GUID erasure cannot allocate or fail. Expand only the inserted layer's parent, preserving other UI state.
    if (prepared._parent)
        SetGroupCollapsed(prepared._parent.value(), false);
    return true;
}

std::uint64_t DocumentSession::ClipboardPastePreparationMemoryBytes(std::int64_t additionalPixels) const noexcept
{
    if (additionalPixels <= 0 || additionalPixels > ClipboardPastePixelLimit())
        return 0;
    auto bytes = history.EditMemoryBytes(_document);
    // History bounds eight existing record/name copies and trim bookkeeping. Include new records/selection
    // nodes, hierarchy/name working sets, and two full pixel owners for an owned decode plus its staging.
    const auto records = _document->layers.size();
    return bytes && AddPasteBytes(bytes, 2ull * 1024 * 1024) &&
                   AddPasteBytes(bytes, static_cast<std::uint64_t>(additionalPixels) * 8) &&
                   AddPasteBytes(bytes, records * (sizeof(Guid) * 8ull + 512)) &&
                   AddPasteBytes(bytes, 8 * (sizeof(ImageLayer) + 4096ull))
               ? bytes
               : 0;
}

std::uint64_t DocumentSession::ClipboardPasteRetirementMemoryBytes() const noexcept
{
    auto bytes = ImageImportRetirementMemoryBytes();
    if (!bytes || (_clipboard && !AddPasteBytes(bytes, _clipboard->image.RetirementMemoryBytes())) ||
        !AddPasteBytes(bytes, sizeof(ClipboardPasteInput) + sizeof(PreparedClipboardPaste) + 4096))
        return 0;
    return bytes;
}
