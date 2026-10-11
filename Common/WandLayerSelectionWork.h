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
    // Zero in a New-only plan. A complete boolean plan owns one fixed workspace and both conversion
    // outputs concurrently with the original trace. Existing immutable input geometry stays in capture.
    std::uint64_t booleanWorkspaceBytes = 0;
    std::uint64_t booleanOutputBytes = 0;
    std::uint64_t booleanPhaseBytes = 0;
    bool transparentShortcut = false;
};

enum class WandLayerSelectionAdmission : std::uint8_t
{
    Ready,
    InvalidMode,
    IncompletePlan,
    Overflow,
    UnknownClipperWorkspace,
};

// A New-only plan still refuses Add/Subtract before capture. A mode-aware plan includes the private
// bounded adapter's complete workspace and output coexistence. Subtract without selection is trace-only.
[[nodiscard]] WandLayerSelectionAdmission EvaluateWandLayerSelectionAdmission(const WandLayerSelectionMemoryPlan& plan,
                                                                              SelectionMode mode,
                                                                              bool hasCurrentSelection,
                                                                              std::uint64_t& requiredBytes) noexcept;
[[nodiscard]] bool TryAddWandLayerSelectionBytes(std::uint64_t left, std::uint64_t right,
                                                 std::uint64_t& result) noexcept;

// Immutable selection-mode capture. Separate entry points bind an active layer or a detached full composite.
// Complete graph admission must precede owner capture; neither entry point owns a GPU device.
class WandLayerSelectionInput final
{
  public:
    WandLayerSelectionInput(const WandLayerSelectionInput&) = delete;
    WandLayerSelectionInput& operator=(const WandLayerSelectionInput&) = delete;
    WandLayerSelectionInput(WandLayerSelectionInput&& source) noexcept
        : _point(source._point), _settings(source._settings), _antialiased(source._antialiased), _mode(source._mode),
          _usesComposite(source._usesComposite), _outputHistoryBytes(source._outputHistoryBytes),
          _authority(std::move(source._authority)), _token(std::move(source._token)),
          _history(std::move(source._history)), _document(std::move(source._document)),
          _active(std::move(source._active)), _valid(std::exchange(source._valid, false))
    {
    }
    WandLayerSelectionInput& operator=(WandLayerSelectionInput&& source) noexcept
    {
        if (this != &source)
        {
            _point = source._point;
            _settings = source._settings;
            _antialiased = source._antialiased;
            _mode = source._mode;
            _usesComposite = source._usesComposite;
            _outputHistoryBytes = source._outputHistoryBytes;
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
    WandLayerSelectionInput(Vec2 point, WandSettings settings, bool antialiased, SelectionMode mode,
                            HistoryRestoreAuthority authority, JobToken token, DocumentHistory::StateView history,
                            CanvasDocument document, std::optional<Guid> active, bool usesComposite = false,
                            std::uint64_t outputHistoryBytes = 0) noexcept
        : _point(point), _settings(settings), _antialiased(antialiased), _mode(mode), _usesComposite(usesComposite),
          _outputHistoryBytes(outputHistoryBytes), _authority(std::move(authority)), _token(std::move(token)),
          _history(std::move(history)), _document(std::move(document)), _active(std::move(active))
    {
    }
    Vec2 _point;
    WandSettings _settings;
    bool _antialiased;
    SelectionMode _mode = SelectionMode::New;
    bool _usesComposite = false;
    std::uint64_t _outputHistoryBytes = 0;
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
    // Worker-sealed output envelope only; the host partitions its original complete reservation.
    [[nodiscard]] std::uint64_t OutputAllocationBytes() const noexcept
    {
        return _outputBytes;
    }
    [[nodiscard]] bool ChangesSelection() const noexcept
    {
        return _history.has_value();
    }
    // Borrow only after worker sealing, before Apply swaps the after-document into the session.
    [[nodiscard]] const DocumentSelection* SelectionForDisplayForWork() const noexcept
    {
        return _document && _document->selection ? &_document->selection.value() : nullptr;
    }

  private:
    friend class DocumentSession;
    explicit PreparedWandLayerSelection(WandLayerSelectionInput input) noexcept : _input(std::move(input)) {}
    WandLayerSelectionInput _input;
    std::optional<CanvasDocument> _document;
    std::optional<DocumentHistory::Publication> _history;
    std::optional<CanvasDocument> _retiredDisplay;
    std::uint64_t _outputBytes = 0;
    bool _applied = false;
};
