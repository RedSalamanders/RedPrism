#pragma once

#include "CopyPixelsWork.h"
#include "RasterEditWork.h"

#include <cstdint>
#include <optional>
#include <utility>

enum class ClipboardCommandKind : std::uint8_t
{
    Copy,
    CopyMerged,
    Cut
};

// Owner capture after graph admission and command-specific settlement. Copy may coexist with live brush,
// warp, outline/lasso and shape drafts; it samples committed target pixels. Cut requires selection interaction
// closure but does not finish a live warp. No worker consults those live drafts or the session clipboard.
class ClipboardCommandInput final
{
  public:
    ClipboardCommandInput(const ClipboardCommandInput&) = delete;
    ClipboardCommandInput& operator=(const ClipboardCommandInput&) = delete;
    ClipboardCommandInput(ClipboardCommandInput&& source) noexcept
        : _kind(source._kind), _token(std::move(source._token)), _copy(std::move(source._copy)),
          _clear(std::move(source._clear)), _document(std::move(source._document)),
          _history(std::move(source._history)), _active(std::move(source._active)), _crop(source._crop),
          _clipboardSerial(source._clipboardSerial), _mask(source._mask), _valid(std::exchange(source._valid, false))
    {
    }
    ClipboardCommandInput& operator=(ClipboardCommandInput&&) = delete;

  private:
    friend class DocumentSession;
    friend class PreparedClipboardCommand;
    ClipboardCommandInput(ClipboardCommandKind kind, JobToken token, CopyPixelsInput copy, std::optional<Guid> active,
                          std::optional<CropRect> crop, std::uint64_t clipboardSerial, bool mask) noexcept
        : _kind(kind), _token(std::move(token)), _copy(std::move(copy)), _active(active), _crop(crop),
          _clipboardSerial(clipboardSerial), _mask(mask)
    {
    }
    ClipboardCommandKind _kind;
    JobToken _token;
    CopyPixelsInput _copy;
    std::optional<RasterEditInput> _clear;
    std::optional<CanvasDocument> _document;
    DocumentHistory::StateView _history;
    std::optional<Guid> _active;
    std::optional<CropRect> _crop;
    std::uint64_t _clipboardSerial;
    bool _mask;
    bool _valid = true;
};

// A sealed copy and optional complete Cut document/history. Apply swaps all replaced owners into this
// aggregate. Failed preparation/validation leaves private clipboard, document and history untouched.
class PreparedClipboardCommand final
{
  public:
    PreparedClipboardCommand(const PreparedClipboardCommand&) = delete;
    PreparedClipboardCommand& operator=(const PreparedClipboardCommand&) = delete;
    PreparedClipboardCommand(PreparedClipboardCommand&&) noexcept = default;
    PreparedClipboardCommand& operator=(PreparedClipboardCommand&&) = delete;
    // Stable new pixels before and after publication, for the subsequent admitted OS encoding stage.
    [[nodiscard]] const PixelClipboard& Clipboard() const noexcept
    {
        return _copied.value();
    }
    [[nodiscard]] bool CutsPixels() const noexcept
    {
        return _history.has_value();
    }

  private:
    friend class DocumentSession;
    explicit PreparedClipboardCommand(ClipboardCommandInput input) noexcept : _input(std::move(input)) {}
    ClipboardCommandInput _input;
    std::optional<PixelClipboard> _clipboard, _copied;
    std::optional<CanvasDocument> _document, _retiredDisplay;
    std::optional<DocumentHistory::Publication> _history;
    std::optional<BrushCommitInfo> _retiredBrushCommit;
    bool _applied = false;
};
