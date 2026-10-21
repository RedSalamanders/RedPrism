#pragma once

#include "HistoryRestoreWork.h"
#include "LassoFinishGeometry.h"

#include <cstdint>
#include <optional>
#include <utility>

// Captured only after the selected terminal intent has closed its draft in bounded time. Full geometry,
// document/history, original arena lifetime, output/refresh overlap and retirement admission comes first.
// The opening check complements the ordinary settled-session and job token authority.
class LassoFinishInput final
{
  public:
    LassoFinishInput(const LassoFinishInput&) = delete;
    LassoFinishInput& operator=(const LassoFinishInput&) = delete;
    LassoFinishInput(LassoFinishInput&& source) noexcept
        : _terminal(std::move(source._terminal)), _authority(std::move(source._authority)),
          _token(std::move(source._token)), _history(std::move(source._history)),
          _document(std::move(source._document)), _active(std::move(source._active)),
          _outputHistoryBytes(source._outputHistoryBytes), _intent(source._intent),
          _valid(std::exchange(source._valid, false))
    {
    }
    LassoFinishInput& operator=(LassoFinishInput&&) = delete;

  private:
    friend class DocumentSession;
    friend class PreparedLassoFinish;
    LassoFinishInput(LassoFinishTerminal terminal, HistoryRestoreAuthority authority, JobToken token,
                     DocumentHistory::StateView history, CanvasDocument document, std::optional<Guid> active,
                     std::uint64_t outputHistoryBytes, std::uint64_t intent) noexcept
        : _terminal(std::move(terminal)), _authority(std::move(authority)), _token(std::move(token)),
          _history(std::move(history)), _document(std::move(document)), _active(std::move(active)),
          _outputHistoryBytes(outputHistoryBytes), _intent(intent)
    {
    }
    LassoFinishTerminal _terminal;
    HistoryRestoreAuthority _authority;
    JobToken _token;
    DocumentHistory::StateView _history;
    std::optional<CanvasDocument> _document;
    std::optional<Guid> _active;
    std::uint64_t _outputHistoryBytes = 0;
    std::uint64_t _intent = 0;
    bool _valid = true;
};

class PreparedLassoFinish final
{
  public:
    PreparedLassoFinish(const PreparedLassoFinish&) = delete;
    PreparedLassoFinish& operator=(const PreparedLassoFinish&) = delete;
    PreparedLassoFinish(PreparedLassoFinish&&) noexcept = default;
    PreparedLassoFinish& operator=(PreparedLassoFinish&&) = delete;
    [[nodiscard]] bool ChangesSelection() const noexcept
    {
        return _history.has_value();
    }
    [[nodiscard]] const DocumentSelection* SelectionForDisplayForWork() const noexcept
    {
        return _document && _document->selection ? &_document->selection.value() : nullptr;
    }
    [[nodiscard]] std::uint64_t OutputAllocationBytes() const noexcept
    {
        return _outputBytes;
    }
    [[nodiscard]] DocumentHistory::ResidencyValidation TryAcquireHistoryResidency(
        DocumentHistory::ResidencyPermit& permit) noexcept;
    [[nodiscard]] bool RefreshHistoryResidency(CancelToken cancellation = {},
                                               DocumentHistory::PrepareObserver observer = {});

  private:
    friend class DocumentSession;
    explicit PreparedLassoFinish(LassoFinishInput input) noexcept : _input(std::move(input)) {}
    LassoFinishInput _input;
    std::optional<CanvasDocument> _document;
    std::optional<DocumentHistory::Publication> _history;
    std::optional<CanvasDocument> _retiredDisplay;
    std::uint64_t _outputBytes = 0;
    bool _applied = false;
};
