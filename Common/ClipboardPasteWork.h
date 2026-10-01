#pragma once

#include "ImageImportHistory.h"

#include <cstdint>
#include <optional>
#include <utility>

// Single-use owner capture after admission. The complete document/history and private pixels are owned,
// including after a stale replacement. Origin selection and layer/history construction happen on the worker.
class ClipboardPasteInput final
{
  public:
    ClipboardPasteInput(const ClipboardPasteInput&) = delete;
    ClipboardPasteInput& operator=(const ClipboardPasteInput&) = delete;
    ClipboardPasteInput(ClipboardPasteInput&& input) noexcept
        : _authority(std::move(input._authority)), _source(std::move(input._source)), _active(input._active),
          _history(std::move(input._history)), _clipboard(std::move(input._clipboard)), _serial(input._serial),
          _valid(std::exchange(input._valid, false))
    {
    }
    ClipboardPasteInput& operator=(ClipboardPasteInput&&) = delete;

  private:
    friend class DocumentSession;
    ClipboardPasteInput(ImageImportAuthority authority, CanvasDocument source, std::optional<Guid> active,
                        DocumentHistory::StateView history, std::optional<PixelClipboard> clipboard,
                        std::uint64_t serial) noexcept
        : _authority(std::move(authority)), _source(std::move(source)), _active(active), _history(std::move(history)),
          _clipboard(std::move(clipboard)), _serial(serial)
    {
    }
    ImageImportAuthority _authority;
    CanvasDocument _source;
    std::optional<Guid> _active;
    DocumentHistory::StateView _history;
    std::optional<PixelClipboard> _clipboard;
    std::uint64_t _serial;
    bool _valid = true;
};

// All new document/history/selection owners exist before application. Old document/history/display owners
// remain in this aggregate for admitted worker retirement. Only the private branch depends on the copy serial.
class PreparedClipboardPaste final
{
  public:
    PreparedClipboardPaste(const PreparedClipboardPaste&) = delete;
    PreparedClipboardPaste& operator=(const PreparedClipboardPaste&) = delete;
    PreparedClipboardPaste(PreparedClipboardPaste&&) noexcept = default;
    PreparedClipboardPaste& operator=(PreparedClipboardPaste&&) = delete;

  private:
    friend class DocumentSession;
    PreparedClipboardPaste(PreparedImageImport publication, std::optional<Guid> parent,
                           std::optional<std::uint64_t> privateSerial) noexcept
        : _publication(std::move(publication)), _parent(parent), _privateSerial(privateSerial)
    {
    }
    PreparedImageImport _publication;
    std::optional<Guid> _parent;
    std::optional<std::uint64_t> _privateSerial;
};
