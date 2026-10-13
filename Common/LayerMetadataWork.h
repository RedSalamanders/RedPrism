#pragma once

#include "HistoryRestoreWork.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

enum class LayerMetadataKind : std::uint8_t
{
    Rename,
    ToggleVisibility,
    SetOpacity,
    SetBlendMode,
};

enum class LayerMetadataPrepareStage : std::uint8_t
{
    BeforeDraft,
    AfterEdit,
    AfterSeal,
    BeforeOpacityOpening,
    AfterOpacityOpening,
};

// Borrowed for one detached worker preparation. Production leaves this empty. No callback consults UI/session.
struct LayerMetadataPrepareObserver final
{
    void (*checkpoint)(void*, LayerMetadataPrepareStage) = nullptr;
    void* context = nullptr;
};

// Owner-captured gesture identity. Workers hold only serials and never inspect or prolong mutable strokes.
// Moving a point keeps authority; closing and reopening a gesture cannot reuse its opening.
class LayerMetadataAuthority final
{
  public:
    LayerMetadataAuthority() = default;

  private:
    friend class DocumentSession;
    HistoryRestoreAuthority _settled;
    std::optional<std::uint64_t> _brushOpening, _warpOpening, _shapeOpening, _lassoOpening;
    bool _preserved = false;
};

// Owner-only capture after admission. The request names its opening target; workers see only an immutable
// document/history view and opaque settled authority. Moving consumes the source, including no-op inputs.
class LayerMetadataInput final
{
  public:
    LayerMetadataInput(const LayerMetadataInput&) = delete;
    LayerMetadataInput& operator=(const LayerMetadataInput&) = delete;
    LayerMetadataInput(LayerMetadataInput&& source) noexcept
        : _kind(source._kind), _target(source._target), _name(std::move(source._name)), _opacity(source._opacity),
          _blendMode(source._blendMode), _authority(std::move(source._authority)), _token(std::move(source._token)),
          _history(std::move(source._history)), _document(std::move(source._document)), _active(source._active),
          _opacityOpening(source._opacityOpening), _opacityPreview(source._opacityPreview),
          _valid(std::exchange(source._valid, false))
    {
    }
    LayerMetadataInput& operator=(LayerMetadataInput&&) = delete;

  private:
    friend class DocumentSession;
    friend class PreparedLayerMetadata;
    LayerMetadataInput(LayerMetadataKind kind, Guid target, std::wstring name, double opacity, LayerBlendMode blendMode,
                       LayerMetadataAuthority authority, JobToken token, DocumentHistory::StateView history,
                       CanvasDocument document, std::optional<Guid> active) noexcept
        : _kind(kind), _target(target), _name(std::move(name)), _opacity(opacity), _blendMode(blendMode),
          _authority(std::move(authority)), _token(std::move(token)), _history(std::move(history)),
          _document(std::move(document)), _active(active)
    {
    }
    LayerMetadataKind _kind;
    Guid _target;
    std::wstring _name;
    double _opacity;
    LayerBlendMode _blendMode;
    LayerMetadataAuthority _authority;
    JobToken _token;
    DocumentHistory::StateView _history;
    std::optional<CanvasDocument> _document;
    std::optional<Guid> _active;
    // Captured owner intent for a typed closure; workers cannot change its opening or turn a preview into history.
    std::optional<double> _opacityOpening;
    bool _opacityPreview = false;
    bool _valid = true;
};

// Complete sealed output. Successful owner publication swaps old document/display/history into this same
// aggregate. The host keeps it under its reservation through acknowledgement and releases it on a worker.
class PreparedLayerMetadata final
{
  public:
    PreparedLayerMetadata(const PreparedLayerMetadata&) = delete;
    PreparedLayerMetadata& operator=(const PreparedLayerMetadata&) = delete;
    PreparedLayerMetadata(PreparedLayerMetadata&&) noexcept = default;
    PreparedLayerMetadata& operator=(PreparedLayerMetadata&&) = delete;
    [[nodiscard]] bool ChangesDocument() const noexcept
    {
        return _document.has_value();
    }
    // Owner-only attempt; no wait/allocation/pixel work. A Busy/Refresh result retains the completed metadata
    // intent for an admitted same-ticket worker stage. This never substitutes for current session authority.
    [[nodiscard]] DocumentHistory::ResidencyValidation TryAcquireHistoryResidency(
        DocumentHistory::ResidencyPermit& permit) noexcept;
    // Worker-only; reuses the sealed output and original revision, never recreates the metadata edit.
    [[nodiscard]] bool RefreshHistoryResidency(CancelToken cancellation = {},
                                               DocumentHistory::PrepareObserver observer = {});

  private:
    friend class DocumentSession;
    explicit PreparedLayerMetadata(LayerMetadataInput input) noexcept : _input(std::move(input)) {}
    LayerMetadataInput _input;
    std::optional<CanvasDocument> _document, _retiredDisplay;
    std::optional<CanvasDocument> _opacityHistoryOpening;
    std::optional<DocumentHistory::Publication> _history;
    bool _applied = false;
};

[[nodiscard]] inline std::optional<PreparedLayerMetadata> PrepareLayerMetadataPublication(
    LayerMetadataInput input, CancelToken cancellation = {}, DocumentHistory::PrepareObserver historyObserver = {},
    LayerMetadataPrepareObserver observer = {}, std::shared_ptr<const SharedPublicationMemory> memory = {})
{
    return DocumentSession::PrepareLayerMetadataPublication(std::move(input), cancellation, historyObserver, observer,
                                                            std::move(memory));
}
