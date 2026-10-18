#pragma once

#include "SelectionCommandWork.h"
#include "SelectionHitWork.h"
#include "SelectionMoveOpeningWork.h"
#include "SelectionMoveWork.h"
#include "SelectionOutlineGestureState.h"
#include "SharedPublicationMemory.h"

#include <cstdint>
#include <memory>
#include <optional>

struct SelectionOutlineDrawing;

class SelectionOutlineGestureJob final : public Job
{
    enum class Phase
    {
        Allocate,
        Open,
        Collect,
        Close,
        Click,
        RefreshClose,
        RefreshClick,
        DrawOpen,
        DrawFinish,
        DrawRefresh
    };

  public:
    explicit SelectionOutlineGestureJob(SelectionOutlineGestureState& state);
    JobResult Run(JobContext& context) override;
    bool DeferCompletion(JobResult result, JobTicket ticket) noexcept override;
    bool NotifyInput() noexcept;
    void Completed(JobResult result) noexcept override;
    void RetainJoinedRetirement(JobScheduler&, JobTicket ticket) noexcept override;

  private:
    bool NotifyMoves(bool changed) noexcept;
    bool ConsumeDrawing() noexcept;
    bool ConsumePolygonal() noexcept;
    bool FinishDrawing(const HostSelectionGestureInput::Sample& sample) noexcept;
    bool DrawingCurrent() const noexcept;
    void AbortDrawing() noexcept;
    bool Refuse(std::int32_t error) noexcept;
    bool Resume() noexcept;
    bool Consume() noexcept;
    SelectionOutlineGestureState& _state;
    const std::optional<LassoFinishGeometry::DrawingPlan> _drawingPolicy;
    Phase _phase = Phase::Allocate;
    std::shared_ptr<SharedPublicationMemory> _output, _final, _temporary;
    std::optional<DocumentReadAuthority> _source;
    SelectionDisplayValue _display;
    Vec2 _point{}, _offset{};
    std::uint64_t _geometryEnvelope = 0;
    std::int32_t _error = 0;
    bool _click = false, _terminal = false;
    std::optional<HostSelectionGestureInput::Event> _lastInput;
    std::uint64_t _lastPolygonalOrdinal = 0;
    std::optional<SelectionMoveOpeningInput> _input;
    std::optional<PreparedSelectionMoveOpening> _opening;
    std::optional<SelectionMoveClosureInput> _closeInput;
    std::optional<PreparedSelectionMoveClosure> _close;
    std::optional<SelectionCommandInput> _commandInput;
    std::optional<PreparedSelectionCommand> _command;
    DocumentHistory::ResidencyPermit _residency;
    std::shared_ptr<SelectionOutlineDrawing> _drawing;
};
