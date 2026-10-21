#pragma once

#include <cstdint>
#include <limits>
#include <optional>

enum class HistoryWorkKind
{
    Commit,
    Selection,
    Stroke,
    Open,
    Import
};

struct HistoryWorkStamp final
{
    const void* owner = nullptr; // identity only; UI-owned state is never accessed by workers
    std::uint64_t documentSerial = 0;
    std::uint64_t ordinal = 0;
    HistoryWorkKind kind = HistoryWorkKind::Commit;
    std::uint64_t acquisition = 0; // a continuation of one ordinal must never alias an earlier acquisition
    [[nodiscard]] bool operator==(const HistoryWorkStamp&) const noexcept = default;
};

// One document's history-writing operations are ordered across scheduler classes. A scheduler serialKey alone
// does not establish this order: it serializes only within one class. Commands and recorded strokes share these
// ordinals; the host selects the oldest input, begins it here, and only then snapshots the current document.
// This object owns no input/pixel storage and never waits. It remains at a stable address until jobs have joined.
class HistoryWork final
{
  public:
    HistoryWork() noexcept = default;
    HistoryWork(const HistoryWork&) = delete;
    HistoryWork& operator=(const HistoryWork&) = delete;

    [[nodiscard]] std::optional<std::uint64_t> NextOrdinal() noexcept
    {
        if (_next == (std::numeric_limits<std::uint64_t>::max)())
        {
            return std::nullopt;
        }
        return ++_next;
    }

    [[nodiscard]] std::optional<HistoryWorkStamp> Begin(std::uint64_t documentSerial, std::uint64_t ordinal,
                                                        HistoryWorkKind kind) noexcept
    {
        if (_active || documentSerial == 0 || ordinal == 0 || ordinal <= _lastStarted || ordinal > _next ||
            _acquisition == (std::numeric_limits<std::uint64_t>::max)())
        {
            return std::nullopt;
        }
        _lastStarted = ordinal;
        _active = HistoryWorkStamp{this, documentSerial, ordinal, kind, ++_acquisition};
        _lastFinished.reset();
        _lastReplacementSerial = 0;
        _cancelRequested = false;
        return _active;
    }

    // A selected request may need a second operation (commit, then import/resize) before a younger input starts.
    // Only the exact latest finished acquisition can continue its ordinal on its current lineage, including an
    // explicitly recorded owner replacement. The host checks prerequisite success/lifetime before calling;
    // this metadata does not itself authorize a dependent action.
    [[nodiscard]] std::optional<HistoryWorkStamp> Continue(HistoryWorkStamp previous, std::uint64_t documentSerial,
                                                           HistoryWorkKind kind) noexcept
    {
        if (_active || !_lastFinished || *_lastFinished != previous || previous.ordinal != _lastStarted ||
            documentSerial == 0 ||
            documentSerial != (_lastReplacementSerial != 0 ? _lastReplacementSerial : previous.documentSerial) ||
            _acquisition == (std::numeric_limits<std::uint64_t>::max)())
        {
            return std::nullopt;
        }
        _active = HistoryWorkStamp{this, documentSerial, previous.ordinal, kind, ++_acquisition};
        _lastFinished.reset();
        _lastReplacementSerial = 0;
        _cancelRequested = false;
        return _active;
    }

    [[nodiscard]] bool IsCurrent(HistoryWorkStamp stamp) const noexcept
    {
        return _active && *_active == stamp;
    }

    // Call only AFTER apply/drop and its history transaction have finished, never on worker completion alone.
    [[nodiscard]] bool Finish(HistoryWorkStamp stamp) noexcept
    {
        if (!IsCurrent(stamp))
        {
            return false;
        }
        _lastFinished = _active;
        _lastReplacementSerial = 0;
        _active.reset();
        _cancelRequested = false;
        return true;
    }

    // Only a validated owner replacement may continue a selected batch on its new session lineage. Retain the
    // exact finished acquisition so no other input or external replacement can authorize that continuation.
    [[nodiscard]] bool FinishReplacement(HistoryWorkStamp stamp, std::uint64_t replacementSerial) noexcept
    {
        if (replacementSerial == 0 || replacementSerial == stamp.documentSerial || !Finish(stamp))
            return false;
        _lastReplacementSerial = replacementSerial;
        return true;
    }

    // Escape cancels Commit/Selection, not a pointer-up stroke. Open/import cancellation has its own file flow.
    [[nodiscard]] std::optional<HistoryWorkStamp> RequestCancel() noexcept
    {
        if (!_active || (_active->kind != HistoryWorkKind::Commit && _active->kind != HistoryWorkKind::Selection))
        {
            return std::nullopt;
        }
        _cancelRequested = true;
        return _active;
    }

    [[nodiscard]] bool CancelRequested(HistoryWorkStamp stamp) const noexcept
    {
        return IsCurrent(stamp) && _cancelRequested;
    }

    [[nodiscard]] bool Busy() const noexcept
    {
        return _active.has_value();
    }

    [[nodiscard]] std::optional<HistoryWorkStamp> Active() const noexcept
    {
        return _active;
    }

  private:
    friend struct HistoryWorkFixture; // Only native fixtures may place the event clock at its boundary.
    std::uint64_t _next = 0;
    std::uint64_t _lastStarted = 0;
    std::uint64_t _acquisition = 0;
    std::optional<HistoryWorkStamp> _active;
    std::optional<HistoryWorkStamp> _lastFinished;
    std::uint64_t _lastReplacementSerial = 0;
    bool _cancelRequested = false;
};

enum class StrokeReplayPhase
{
    Recording,
    Pending,
    Running,
    Applied,
    CancelledInput,
    Failed
};

// State embedded alongside each owned stroke recording (parameters, samples, seed and target). The recording is
// retained through apply; this state does not copy, discard, cap, or allocate input storage. P1's stroke path owns
// that storage and P0.7 accounts for it. Pointer drawing can continue while preceding history work runs.
class StrokeReplay final
{
  public:
    StrokeReplay(std::uint64_t documentSerial, std::uint64_t ordinal) noexcept
        : _documentSerial(documentSerial), _ordinal(ordinal)
    {
    }
    StrokeReplay(const StrokeReplay&) = delete;
    StrokeReplay& operator=(const StrokeReplay&) = delete;

    [[nodiscard]] bool EndInput() noexcept
    {
        if (_phase != StrokeReplayPhase::Recording)
        {
            return false;
        }
        _phase = StrokeReplayPhase::Pending;
        return true;
    }

    [[nodiscard]] bool CancelInput() noexcept
    {
        if (_phase != StrokeReplayPhase::Recording)
        {
            return false;
        }
        _phase = StrokeReplayPhase::CancelledInput;
        return true;
    }

    // earlierInput is the smallest ordinal in the command queue or other stroke recordings (zero: none).
    // The snapshot/selection/token MUST be taken after this returns a stamp. If a predecessor failed/cancelled,
    // the surviving current document automatically becomes the replay base; no stale preview is committed.
    [[nodiscard]] std::optional<HistoryWorkStamp> Begin(HistoryWork& history, std::uint64_t currentDocumentSerial,
                                                        std::uint64_t earlierInput = 0) noexcept
    {
        if (_phase != StrokeReplayPhase::Pending || currentDocumentSerial != _documentSerial ||
            (earlierInput != 0 && earlierInput < _ordinal))
        {
            return std::nullopt;
        }
        _stamp = history.Begin(_documentSerial, _ordinal, HistoryWorkKind::Stroke);
        if (_stamp)
        {
            _phase = StrokeReplayPhase::Running;
        }
        return _stamp;
    }

    // Failed strokes keep their recording and enter a visible failed state; never silently drop input or retry
    // in an idle loop. A retry is a new ordered user action with a fresh ordinal after memory/device recovery.
    [[nodiscard]] bool Finish(HistoryWork& history, HistoryWorkStamp stamp, bool applied) noexcept
    {
        if (_phase != StrokeReplayPhase::Running || !_stamp || *_stamp != stamp || !history.Finish(stamp))
        {
            return false;
        }
        _phase = applied ? StrokeReplayPhase::Applied : StrokeReplayPhase::Failed;
        _stamp.reset();
        return true;
    }

    [[nodiscard]] bool Retry(std::uint64_t ordinal) noexcept
    {
        if (_phase != StrokeReplayPhase::Failed || ordinal <= _ordinal)
        {
            return false;
        }
        _ordinal = ordinal;
        _phase = StrokeReplayPhase::Pending;
        return true;
    }

    [[nodiscard]] StrokeReplayPhase Phase() const noexcept
    {
        return _phase;
    }
    [[nodiscard]] std::uint64_t Ordinal() const noexcept
    {
        return _ordinal;
    }

  private:
    std::uint64_t _documentSerial;
    std::uint64_t _ordinal;
    StrokeReplayPhase _phase = StrokeReplayPhase::Recording;
    std::optional<HistoryWorkStamp> _stamp;
};
