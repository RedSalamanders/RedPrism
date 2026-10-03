#pragma once

#include "HistoryRestoreWork.h"

#include <cstdint>
#include <optional>
#include <utility>

// Whole-pixel event input. Geometry work counts both loops and points, with checkpoints at most 1024 apart.
enum class SelectionNudgeStage : std::uint8_t
{
    Begin,
    Translate,
    Complete
};
struct SelectionNudgeObserver final
{
    void* context = nullptr;
    void (*checkpoint)(void*, SelectionNudgeStage, std::uint64_t) noexcept = nullptr;
};

class SelectionNudgeInput final
{
  public:
    SelectionNudgeInput(const SelectionNudgeInput&) = delete;
    SelectionNudgeInput& operator=(const SelectionNudgeInput&) = delete;
    SelectionNudgeInput(SelectionNudgeInput&& source) noexcept
        : _authority(std::move(source._authority)), _token(std::move(source._token)),
          _history(std::move(source._history)), _document(std::move(source._document)),
          _active(std::move(source._active)), _opening(source._opening), _dx(source._dx), _dy(source._dy),
          _valid(std::exchange(source._valid, false))
    {
    }
    SelectionNudgeInput& operator=(SelectionNudgeInput&& source) noexcept
    {
        if (this != &source)
        {
            _authority = std::move(source._authority);
            _token = std::move(source._token);
            _history = std::move(source._history);
            _document = std::move(source._document);
            _active = std::move(source._active);
            _opening = source._opening;
            _dx = source._dx;
            _dy = source._dy;
            _valid = std::exchange(source._valid, false);
        }
        return *this;
    }

  private:
    friend class DocumentSession;
    friend class PreparedSelectionNudge;
    SelectionNudgeInput(HistoryRestoreAuthority authority, JobToken token, DocumentHistory::StateView history,
                        CanvasDocument document, std::optional<Guid> active, std::uint64_t opening, int dx,
                        int dy) noexcept
        : _authority(std::move(authority)), _token(std::move(token)), _history(std::move(history)),
          _document(std::move(document)), _active(std::move(active)), _opening(opening), _dx(dx), _dy(dy)
    {
    }
    HistoryRestoreAuthority _authority;
    JobToken _token;
    DocumentHistory::StateView _history;
    std::optional<CanvasDocument> _document;
    std::optional<Guid> _active;
    std::uint64_t _opening;
    int _dx;
    int _dy;
    bool _valid = true;
};

class PreparedSelectionNudge final
{
  public:
    PreparedSelectionNudge(const PreparedSelectionNudge&) = delete;
    PreparedSelectionNudge& operator=(const PreparedSelectionNudge&) = delete;
    PreparedSelectionNudge(PreparedSelectionNudge&&) noexcept = default;
    PreparedSelectionNudge& operator=(PreparedSelectionNudge&&) noexcept = default;

  private:
    friend class DocumentSession;
    explicit PreparedSelectionNudge(SelectionNudgeInput input) noexcept : _input(std::move(input)) {}
    SelectionNudgeInput _input;
    std::optional<CanvasDocument> _document;
    std::optional<DocumentHistory::Publication> _history;
    std::optional<CanvasDocument> _retiredDisplay;
    bool _applied = false;
};
