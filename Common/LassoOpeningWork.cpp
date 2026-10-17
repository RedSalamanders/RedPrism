#include "LassoOpeningWork.h"

#include "LassoFinishGeometry.h"
#include "LassoFinishWork.h"
#include "LassoIntent.h"
#include "LocallyOwnedPublication.h"
#include "SharedPublicationMemory.h"

#include <cmath>
#include <limits>
#include <utility>

std::size_t LassoDraft::PointCount() const noexcept
{
    return _bounded ? _bounded->PointCountOwner() : points.size();
}
std::optional<Vec2> LassoDraft::FirstPoint() const noexcept
{
    return _bounded ? _bounded->FirstOwner() : points.empty() ? std::nullopt : std::optional(points.front());
}
std::uint64_t LassoDraft::RetirementMemoryBytes() const noexcept
{
    if (_bounded)
        return _boundedRetirementBytes;
    if (points.capacity() > (std::numeric_limits<std::uint64_t>::max)() / sizeof(Vec2))
        return 0;
    return static_cast<std::uint64_t>(points.capacity()) * sizeof(Vec2);
}
std::uint64_t PreparedLassoOpening::MemoryBytes(std::uint32_t blocks) noexcept
{
    const auto arena = BoundedLassoPoints::MemoryBytes(blocks);
    const auto writer = LocallyOwnedPublicationAllowance<BoundedLassoGesture>();
    if (!arena || writer > (std::numeric_limits<std::uint64_t>::max)() - arena)
        return 0;
    return arena + writer;
}
std::optional<LassoOpeningInput> DocumentSession::PrepareLassoOpeningInput(Vec2 point, SelectionMode mode, bool marquee)
{
    return CaptureLassoOpeningInput(point, mode, marquee ? marqueeKind : lassoKind, marquee, std::nullopt);
}
std::optional<LassoOpeningInput> DocumentSession::PrepareLassoOpeningInput(Vec2 point, SelectionMode mode,
                                                                           LassoOpeningSettings settings)
{
    const bool marquee = settings.kind == LassoKind::Rectangle || settings.kind == LassoKind::Ellipse;
    return CaptureLassoOpeningInput(point, mode, settings.kind, marquee, settings.antialiased);
}
std::optional<LassoOpeningInput> DocumentSession::CaptureLassoOpeningInput(Vec2 point, SelectionMode mode,
                                                                           LassoKind kind, bool marquee,
                                                                           std::optional<bool> capturedAntialiased)
{
    if (_lassoDraft || !CanEditSelection() || _selectionMoveOrigin || !std::isfinite(point.x) ||
        !std::isfinite(point.y) ||
        (_lassoOpening == (std::numeric_limits<std::uint64_t>::max)() ||
         _lassoOpeningIntentSerial == (std::numeric_limits<std::uint64_t>::max)()))
        return std::nullopt;
    const bool validKind = marquee ? (kind == LassoKind::Rectangle || kind == LassoKind::Ellipse)
                                   : (kind == LassoKind::Freehand || kind == LassoKind::Polygonal);
    if (!validKind || (mode != SelectionMode::New && mode != SelectionMode::Add && mode != SelectionMode::Subtract))
        return std::nullopt;
    const auto authority = CaptureHistoryRestoreAuthority();
    if (!CanApplyHistoryRestore(authority))
        return std::nullopt;
    const auto token = CaptureJobToken();
    if (!token)
        return std::nullopt;
    const auto intent = ++_lassoOpeningIntentSerial;
    _lassoOpeningPendingSerial = intent;
    return LassoOpeningInput(authority, history.CaptureAuthority(), token.value(), point, mode, kind, marquee,
                             _lassoOpening + 1, intent, capturedAntialiased);
}
std::optional<PreparedLassoOpening> DocumentSession::PrepareLassoOpening(
    LassoOpeningInput input, std::uint32_t blocks, CancelToken cancellation,
    std::shared_ptr<const SharedPublicationMemory> memory, bool retainOwnGesture)
{
    const auto bytes = PreparedLassoOpening::MemoryBytes(blocks);
    if (!input._valid || !input._opening || !bytes || !memory || cancellation.IsCancelled())
        return std::nullopt;
    auto points = BoundedLassoPoints::CreateForWork(memory, blocks, cancellation);
    if (!points || cancellation.IsCancelled())
        return std::nullopt;
    PreparedLassoOpening prepared(std::move(input));
    prepared._ownGesture = retainOwnGesture;
    // The arena has its own original accounted publication. The writer declares only its new control/object;
    // the conservative retirement bound includes both, without charging the shared arena twice.
    prepared._writer =
        MakeLocallyOwnedPublicationObject<BoundedLassoGesture>(memory, bytes, 0, std::move(points.value()));
    if (!prepared._writer || cancellation.IsCancelled())
        return std::nullopt;
    prepared._retirementBytes = bytes;
    prepared._memory = std::move(memory);
    return prepared;
}
bool DocumentSession::IsPreparedLassoOpeningCurrent(const PreparedLassoOpening& prepared) const noexcept
{
    return !prepared._applied && prepared._input._valid && prepared._writer && !_lassoDraft &&
           _lassoOpening != (std::numeric_limits<std::uint64_t>::max)() &&
           prepared._input._opening == _lassoOpening + 1 && prepared._input._intent &&
           prepared._input._intent == _lassoOpeningPendingSerial && history.IsCurrent(prepared._input._history) &&
           CanEditSelection() &&
           (prepared._input._capturedAntialiased.has_value() ||
            prepared._input._kind == (prepared._input._marquee ? marqueeKind : lassoKind)) &&
           CanApplyHistoryRestore(prepared._input._authority) && CanApplyJobResult(prepared._input._token, 0);
}
bool DocumentSession::ApplyPreparedLassoOpeningIfCurrent(PreparedLassoOpening& prepared) noexcept
{
    if (!prepared._memory || !prepared._memory->IsBound() || !IsPreparedLassoOpeningCurrent(prepared) ||
        !prepared._writer->BeginOwner(prepared._input._opening, prepared._input._kind, prepared._input._mode,
                                      prepared._input._capturedAntialiased.value_or(selectionAntialiased),
                                      prepared._input._point))
        return false;
    LassoDraft draft;
    draft.mode = prepared._input._mode;
    draft.kind = prepared._input._kind;
    draft.anchor = prepared._input._marquee
                       ? std::optional(Vec2{std::round(prepared._input._point.x), std::round(prepared._input._point.y)})
                       : std::nullopt;
    if (prepared._ownGesture)
        draft._bounded = prepared._writer; // Original parent retains its exact opaque writer until retirement.
    else
        draft._bounded = std::move(prepared._writer); // Reference/cold opening keeps its original ownership graph.
    draft._boundedRetirementBytes = prepared._retirementBytes;
    _lassoOpening = prepared._input._opening;
    InvalidateLassoPendingIntents();
    _lassoDraft.emplace(std::move(draft));
    if (prepared._ownGesture)
    {
        prepared._gestureKey = prepared._writer->CurrentOwner();
        prepared._gestureActive = true;
    }
    prepared._applied = true;
    return true;
}
bool DocumentSession::IsPreparedLassoGestureCurrent(const PreparedLassoOpening& prepared) const noexcept
{
    return prepared._ownGesture && prepared._applied && prepared._gestureActive && prepared._input._valid &&
           prepared._writer && _lassoDraft && _lassoDraft->_bounded == prepared._writer &&
           _lassoOpening == prepared._input._opening && prepared._writer->ActiveOwner() &&
           prepared._writer->CurrentOwner() == prepared._gestureKey &&
           CanApplyHistoryRestoreWithoutLasso(prepared._input._authority) &&
           history.IsCurrent(prepared._input._history) && CanApplyJobResult(prepared._input._token, 0);
}

bool DocumentSession::DragPreparedMarqueeIfCurrent(PreparedLassoOpening& prepared, Vec2 point, bool square,
                                                   bool fromCenter) noexcept
{
    if (!prepared._input._marquee || !IsPreparedLassoGestureCurrent(prepared) ||
        !DragBoundedMarquee(point, square, fromCenter))
        return false;
    prepared._gestureKey = prepared._writer->CurrentOwner();
    return true;
}

bool DocumentSession::AbortPreparedLassoGestureIfCurrent(PreparedLassoOpening& prepared) noexcept
{
    if (!IsPreparedLassoGestureCurrent(prepared))
        return false;
    CancelLasso();
    prepared._gestureActive = false;
    return true;
}

std::optional<LassoFinishTerminal> DocumentSession::ClosePreparedLassoGestureForWork(PreparedLassoOpening& prepared,
                                                                                     bool terminalAntialiased) noexcept
{
    if (!IsPreparedLassoGestureCurrent(prepared))
        return std::nullopt;
    auto terminal = CloseBoundedLassoForWork(terminalAntialiased);
    if (terminal)
        prepared._gestureActive = false;
    return terminal;
}

std::optional<LassoFinishTerminal> DocumentSession::CaptureBoundedLassoDisplayForWork() const noexcept
{
    if (!_lassoDraft || !_lassoDraft->_bounded || !_lassoDraft->_bounded->ActiveOwner())
        return std::nullopt;
    return LassoFinishTerminal{_lassoDraft->_bounded->CaptureOwner()};
}
std::optional<LassoFinishTerminal> DocumentSession::CloseBoundedLassoForWork(
    std::optional<bool> finishAntialiased) noexcept
{
    if (!_lassoDraft || !_lassoDraft->_bounded)
        return std::nullopt;
    const auto authority = CaptureDocumentReadAuthority();
    if (!authority)
        return std::nullopt;
    auto terminal = _lassoDraft->_bounded->FinishOwner(_lassoDraft->_bounded->CurrentOwner(),
                                                       finishAntialiased.value_or(selectionAntialiased));
    if (!terminal)
        return std::nullopt;
    _lassoClosedOpening = terminal->key.opening;
    _lassoClosedRevision = terminal->key.revision;
    _lassoClosedArenaIdentity = terminal->points.ArenaIdentity();
    _lassoClosedWriter = _lassoDraft->_bounded;
    _lassoClosedSource = authority;
    _lassoClosedActive = _activeLayerID;
    _lassoClosedCrop = _cropRect;
    _lassoClosedMask = _maskSelected;
    _lassoClosedHistory = history.CaptureState();
    _lassoDraft.reset(); // Original accounted writer and arena retire through their original owner sink.
    return LassoFinishTerminal{std::move(terminal.value())};
}

// The host acknowledges a false result as a failed gesture; it must not silently drop an accepted sample.
bool DocumentSession::ExtendBoundedLasso(Vec2 point) noexcept
{
    return _lassoDraft && _lassoDraft->_bounded &&
           _lassoDraft->_bounded->ExtendOwner(_lassoDraft->_bounded->CurrentOwner(), point) !=
               BoundedLassoGesture::Outcome::Refused;
}
bool DocumentSession::MoveBoundedLassoCursor(std::optional<Vec2> point) noexcept
{
    if (!_lassoDraft || !_lassoDraft->_bounded ||
        _lassoDraft->_bounded->CursorOwner(_lassoDraft->_bounded->CurrentOwner(), point) ==
            BoundedLassoGesture::Outcome::Refused)
        return false;
    _lassoDraft->cursor = point;
    return true;
}
bool DocumentSession::DragBoundedMarquee(Vec2 point, bool square, bool fromCenter) noexcept
{
    return _lassoDraft && _lassoDraft->_bounded &&
           _lassoDraft->_bounded->MarqueeOwner(_lassoDraft->_bounded->CurrentOwner(), point, square, fromCenter) !=
               BoundedLassoGesture::Outcome::Refused;
}
bool DocumentSession::RemoveLastBoundedLassoPoint() noexcept
{
    if (!_lassoDraft || !_lassoDraft->_bounded ||
        _lassoDraft->_bounded->PopOwner(_lassoDraft->_bounded->CurrentOwner()) == BoundedLassoGesture::Outcome::Refused)
        return false;
    if (!_lassoDraft->_bounded->PointCountOwner())
        _lassoDraft.reset();
    return true;
}

std::optional<LassoFinishInput> DocumentSession::PrepareClosedBoundedLassoFinishInput(LassoFinishTerminal terminal)
{
    const auto& display = terminal.display;
    if (_lassoDraft || _lassoOpeningPendingSerial || !_lassoClosedRevision || !_lassoClosedSource ||
        !_lassoClosedWriter || !_lassoClosedWriter->MatchesTerminalOwner(display) ||
        display.key.opening != _lassoOpening || display.key.opening != _lassoClosedOpening ||
        display.key.revision != _lassoClosedRevision || display.points.ArenaIdentity() != _lassoClosedArenaIdentity ||
        CaptureDocumentReadAuthority() != _lassoClosedSource || _activeLayerID != _lassoClosedActive ||
        _cropRect != _lassoClosedCrop || _maskSelected != _lassoClosedMask || !_lassoClosedHistory ||
        !history.IsCurrent(_lassoClosedHistory.value()))
        return std::nullopt;
    const bool antialiasedAtClose = display.antialiased;
    auto input = PrepareLassoFinishInput(std::move(terminal));
    if (!input)
        return std::nullopt;
    input->_terminal.display.antialiased = antialiasedAtClose;
    _lassoClosedRevision = 0; // One selected terminal capture; residency refresh reuses the original prepared input.
    _lassoClosedArenaIdentity = nullptr;
    _lassoClosedHistory.reset();
    _lassoClosedWriter.reset();
    return input;
}

void DocumentSession::InvalidateLassoPendingIntents() noexcept
{
    _lassoOpeningPendingSerial = 0;
    _lassoFinishPendingSerial = 0;
    _lassoClosedRevision = 0;
    _lassoClosedArenaIdentity = nullptr;
    _lassoClosedWriter.reset();
    _lassoClosedSource.reset();
    _lassoClosedHistory.reset();
}

bool DocumentSession::SetLassoOpeningCountersForTest(std::uint64_t opening, std::uint64_t intent) noexcept
{
    if (_lassoDraft || _lassoOpeningPendingSerial || _lassoFinishPendingSerial || _lassoClosedRevision ||
        opening < _lassoOpening || intent < _lassoOpeningIntentSerial)
        return false;
    _lassoOpening = opening;
    _lassoOpeningIntentSerial = intent;
    return true;
}

std::optional<LassoIntent> DocumentSession::CaptureLassoIntent(const LassoOpeningInput& input) const noexcept
{
    if (!input._valid || !input._intent || input._authority._session != _tokenIdentity.value ||
        input._authority._documentSerial != _documentSerial || input._intent != _lassoOpeningPendingSerial)
        return std::nullopt;
    return LassoIntent(_tokenIdentity.value, _documentSerial, input._intent, true);
}
std::optional<LassoIntent> DocumentSession::CaptureLassoIntent(const LassoFinishInput& input) const noexcept
{
    if (!input._valid || !input._intent || input._authority._session != _tokenIdentity.value ||
        input._authority._documentSerial != _documentSerial || input._intent != _lassoFinishPendingSerial)
        return std::nullopt;
    return LassoIntent(_tokenIdentity.value, _documentSerial, input._intent, false);
}
bool DocumentSession::DiscardLassoIntentIfCurrent(const LassoIntent& intent) noexcept
{
    if (intent._session != _tokenIdentity.value || intent._documentSerial != _documentSerial || !intent._serial)
        return false;
    auto& pending = intent._opening ? _lassoOpeningPendingSerial : _lassoFinishPendingSerial;
    if (pending != intent._serial)
        return false;
    pending = 0;
    return true;
}
bool DocumentSession::DiscardPreparedClosedLassoIfCurrent(PreparedLassoOpening& prepared) noexcept
{
    if (!prepared._ownGesture || !prepared._applied || prepared._gestureActive || !prepared._writer ||
        _lassoClosedWriter != prepared._writer || !_lassoClosedRevision ||
        _lassoClosedOpening != prepared._input._opening || prepared._retiredClosedHistory)
        return false;
    // The prepared owner keeps both writer and replaced history roots alive for original-ticket retirement.
    prepared._retiredClosedHistory.swap(_lassoClosedHistory);
    _lassoClosedRevision = 0;
    _lassoClosedArenaIdentity = nullptr;
    _lassoClosedWriter.reset();
    _lassoClosedSource.reset();
    return true;
}
