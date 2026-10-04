#pragma once

#include "HistoryRestoreWork.h"

#include <cstdint>
#include <optional>
#include <utility>

enum class WandLayerSelectionStage : std::uint8_t
{
    BeforeSource,
    SourceReady,
    OutlineReady,
    SelectionReady,
    DocumentReady,
};

struct WandLayerSelectionObserver
{
    void* context = nullptr;
    void (*checkpoint)(void*, WandLayerSelectionStage) noexcept = nullptr;
};

// Conservative declared storage, not process commit/RSS or an allocator latency guarantee. Phases reuse the
// same reservation; retained existing owners are charged again for complete aggregate retirement. The pinned
// MSVC v145 default allocator/growth policy is part of the capacity proof. Requalify on a toolset change.
struct WandLayerSelectionMemoryPlan
{
    std::uint64_t captureHistoryBytes = 0;
    std::uint64_t sampledPixelsBytes = 0;
    std::uint64_t privateSourceBytes = 0;
    std::uint64_t maskBytes = 0;
    std::uint64_t vertexBytes = 0;
    std::uint64_t floodGrowthBytes = 0;
    std::uint64_t geometryGrowthBytes = 0;
    std::uint64_t sourcePhaseBytes = 0;
    std::uint64_t maskPhaseBytes = 0;
    std::uint64_t tracePhaseBytes = 0;
    std::uint64_t preparationBytes = 0;
    std::uint64_t retirementBytes = 0;
    std::uint64_t reservationBytes = 0;
    bool transparentShortcut = false;
};

// Immutable New-selection capture. Separate entry points bind an active layer or a detached full composite.
// Complete graph admission must precede owner capture; neither entry point owns a GPU device.
class WandLayerSelectionInput final
{
  public:
    WandLayerSelectionInput(const WandLayerSelectionInput&) = delete;
    WandLayerSelectionInput& operator=(const WandLayerSelectionInput&) = delete;
    WandLayerSelectionInput(WandLayerSelectionInput&& source) noexcept
        : _point(source._point), _settings(source._settings), _antialiased(source._antialiased),
          _usesComposite(source._usesComposite), _authority(std::move(source._authority)),
          _token(std::move(source._token)), _history(std::move(source._history)),
          _document(std::move(source._document)), _active(std::move(source._active)),
          _valid(std::exchange(source._valid, false))
    {
    }
    WandLayerSelectionInput& operator=(WandLayerSelectionInput&& source) noexcept
    {
        if (this != &source)
        {
            _point = source._point;
            _settings = source._settings;
            _antialiased = source._antialiased;
            _usesComposite = source._usesComposite;
            _authority = std::move(source._authority);
            _token = std::move(source._token);
            _history = std::move(source._history);
            _document = std::move(source._document);
            _active = std::move(source._active);
            _valid = std::exchange(source._valid, false);
        }
        return *this;
    }
    // Borrow only while this valid capture is owned. The host admits the entire graph before taking it.
    [[nodiscard]] const CanvasDocument* ReadbackDocument() const noexcept
    {
        return _valid && _document ? &_document.value() : nullptr;
    }

  private:
    friend class DocumentSession;
    friend class PreparedWandLayerSelection;
    WandLayerSelectionInput(Vec2 point, WandSettings settings, bool antialiased, HistoryRestoreAuthority authority,
                            JobToken token, DocumentHistory::StateView history, CanvasDocument document,
                            std::optional<Guid> active, bool usesComposite = false) noexcept
        : _point(point), _settings(settings), _antialiased(antialiased), _usesComposite(usesComposite),
          _authority(std::move(authority)), _token(std::move(token)), _history(std::move(history)),
          _document(std::move(document)), _active(std::move(active))
    {
    }
    Vec2 _point;
    WandSettings _settings;
    bool _antialiased;
    bool _usesComposite = false;
    HistoryRestoreAuthority _authority;
    JobToken _token;
    DocumentHistory::StateView _history;
    std::optional<CanvasDocument> _document;
    std::optional<Guid> _active;
    bool _valid = true;
};

class PreparedWandLayerSelection final
{
  public:
    PreparedWandLayerSelection(const PreparedWandLayerSelection&) = delete;
    PreparedWandLayerSelection& operator=(const PreparedWandLayerSelection&) = delete;
    PreparedWandLayerSelection(PreparedWandLayerSelection&&) noexcept = default;
    PreparedWandLayerSelection& operator=(PreparedWandLayerSelection&&) noexcept = default;
    [[nodiscard]] bool ChangesSelection() const noexcept
    {
        return _history.has_value();
    }

  private:
    friend class DocumentSession;
    explicit PreparedWandLayerSelection(WandLayerSelectionInput input) noexcept : _input(std::move(input)) {}
    WandLayerSelectionInput _input;
    std::optional<CanvasDocument> _document;
    std::optional<DocumentHistory::Publication> _history;
    std::optional<CanvasDocument> _retiredDisplay;
    bool _applied = false;
};
