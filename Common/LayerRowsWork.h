#pragma once

#include "DeferredRelease.h"
#include "Document.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

class LayerRowsWork;

// Capture-free O(1) envelope. Reserve before retaining the root/collapse snapshot/localized pattern. Collapse
// count is bounded by 10,000 display ids; deleted groups may leave remembered ids. This primitive does not
// authorize an unbounded owner copy.
class LayerRowsAdmission final
{
  public:
    [[nodiscard]] static LayerRowsAdmission Estimate(const CanvasDocument& source, std::size_t collapsedCount,
                                                     std::wstring_view hiddenPattern) noexcept;
    [[nodiscard]] bool IsValid() const noexcept
    {
        return _bytes != 0;
    }
    [[nodiscard]] std::uint64_t Bytes() const noexcept
    {
        return _bytes;
    }

  private:
    friend class LayerRowsWork;
    Guid _documentId;
    const void* _layers = nullptr;
    std::size_t _count = 0;
    std::size_t _collapsedCount = 0;
    std::size_t _patternUnits = 0;
    std::size_t _slots = 0;
    std::uint64_t _sourceBytes = 0;
    std::uint64_t _metadataBytes = 0;
    std::uint64_t _bytes = 0;
};

struct LayerRowsObserver final
{
    void (*checkpoint)(void* context, std::size_t rowCount) noexcept = nullptr;
    void* context = nullptr;
};

#pragma warning(push)
#pragma warning(disable : 4324)
// Prepared immutable rows, labels and Guid lookup. Construction/name formatting/hierarchy traversal and last
// destruction belong to workers. Publishing the handle is constant-size. The future host must separately bind
// original document/collapse/locale authority, charge retained rows and qualify DxUi notification/layout costs.
class LayerRowsWork final : public DeferredReleaseNode
{
  private:
    struct ConstructionTag final
    {
    };

  public:
    struct Release final
    {
        void operator()(LayerRowsWork* rows) const noexcept;
    };
    using Handle = std::unique_ptr<LayerRowsWork, Release>;
    explicit LayerRowsWork(ConstructionTag) noexcept {}
    LayerRowsWork(const LayerRowsWork&) = delete;
    LayerRowsWork& operator=(const LayerRowsWork&) = delete;
    LayerRowsWork(LayerRowsWork&&) = delete;
    LayerRowsWork& operator=(LayerRowsWork&&) = delete;
    // Worker only after admission. Top-first hierarchy semantics match LayerRows; collapsed descendants are
    // absent and effective visibility includes every ancestor. Pattern comes from the STRINGTABLE owner.
    [[nodiscard]] static Handle Prepare(CanvasDocument source, std::vector<Guid> collapsed, std::wstring hiddenPattern,
                                        const LayerRowsAdmission& admission, CancelToken cancellation = {},
                                        LayerRowsObserver observer = {});
    [[nodiscard]] std::span<const HierarchyEntry> Rows() const noexcept
    {
        return _rows;
    }
    [[nodiscard]] std::span<const std::wstring> DisplayText() const noexcept
    {
        return _displayText;
    }
    [[nodiscard]] const CanvasDocument& Source() const noexcept
    {
        return _source;
    }
    [[nodiscard]] std::span<const Guid> Collapsed() const noexcept
    {
        return _collapsed;
    }
    [[nodiscard]] std::wstring_view HiddenPattern() const noexcept
    {
        return _hiddenPattern;
    }
    // Owner lookup takes at most 14 Guid comparisons for 10,000 layers, including adversarial hash collisions;
    // it borrows the held root and prepared index, never scans names/rows.
    [[nodiscard]] std::optional<std::size_t> FindVisibleRow(const Guid& id) const noexcept;

  private:
    CanvasDocument _source;
    std::vector<Guid> _collapsed;
    std::wstring _hiddenPattern;
    std::vector<HierarchyEntry> _rows;
    std::vector<std::wstring> _displayText;
    std::vector<std::size_t> _idTable;
    std::vector<std::size_t> _rowByLayer;
    std::vector<std::size_t> _lookupOrder;
};
#pragma warning(pop)
