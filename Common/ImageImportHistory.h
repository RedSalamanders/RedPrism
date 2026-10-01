#pragma once

#include "HistoryRestoreWork.h"
#include "ImageImportWork.h"

#include <memory>
#include <optional>
#include <unordered_set>

// File operations can replace an unsettled session. Undo/Redo authority stays separate and settled-only.
// Draft-only preview publication may finish while IO runs; committed document/history changes still refuse apply.
class ImageImportAuthority final
{
  public:
    ImageImportAuthority() = default;

  private:
    friend class DocumentSession;
    Guid _session;
    std::uint64_t _documentSerial = 0, _generation = 0, _restoreSerial = 0;
    std::optional<Guid> _documentID, _active;
    std::optional<CropRect> _crop;
    bool _maskSelected = false, _valid = false;
};

// CPU-only publication: baseline, final document, history and all owner replacements are prepared together.
class PreparedImageImport final
{
  public:
    PreparedImageImport(const PreparedImageImport&) = delete;
    PreparedImageImport& operator=(const PreparedImageImport&) = delete;
    PreparedImageImport(PreparedImageImport&&) noexcept = default;
    PreparedImageImport& operator=(PreparedImageImport&&) noexcept = default;
    [[nodiscard]] bool ReplacesDocument() const noexcept
    {
        return _replacement.has_value();
    }
    [[nodiscard]] bool ChangesDocument() const noexcept
    {
        return _changesDocument;
    }

  private:
    friend class DocumentSession;
    PreparedImageImport(ImageImportAuthority authority, DocumentHistory::StateView view)
        : _authority(std::move(authority)), _view(std::move(view))
    {
    }
    struct RetiredDrafts final
    {
        std::optional<TransformEdit> transform;
        std::optional<LassoDraft> lasso;
        std::optional<DocumentSelection> selectionMove;
        std::shared_ptr<BrushStroke> brush;
        std::shared_ptr<WarpStroke> warp;
        std::optional<GradientEdit> gradient;
        std::optional<ShapeDraft> shape;
        std::optional<PixelMove> pixelMove;
        std::optional<ColorEdit> color;
    };
    ImageImportAuthority _authority;
    std::unique_ptr<RetiredDrafts> _retiredDrafts;
    DocumentHistory::StateView _view;
    std::optional<DocumentHistory::Publication> _history;
    std::optional<DocumentHistory::ReplacementPublication> _replacement;
    std::optional<CanvasDocument> _source, _baseline, _document, _retiredDisplay;
    std::optional<Guid> _active;
    std::unordered_set<Guid> _selected, _retiredCollapsed;
    std::optional<BrushCommitInfo> _retiredBrushCommit;
    std::optional<LayerAdjustment> _retiredAdjustment;
    SnapGuides _retiredSnapGuides;
    Raster _retiredWarpImage;
    bool _maskSelected = false, _changesDocument = false;
};
