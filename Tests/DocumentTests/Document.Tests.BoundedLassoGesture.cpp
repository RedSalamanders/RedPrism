#include "Document.Tests.BoundedLassoGesture.h"

#include "BoundedLassoGesture.h"
#include "SharedPublicationMemory.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace
{
int gAssertions = 0;
int gFailures = 0;

void Expect(bool condition, const char* label) noexcept
{
    ++gAssertions;
    gFailures += condition ? 0 : 1;
    std::printf("%s bounded-lasso-gesture.%s\n", condition ? "PASS" : "FAIL", label);
}

std::optional<BoundedLassoGesture> NewGesture(const std::shared_ptr<SharedPublicationMemory>& memory,
                                              std::uint32_t blocks = 2)
{
    auto points = BoundedLassoPoints::CreateForWork(memory, blocks);
    if (!points)
        return std::nullopt;
    return BoundedLassoGesture(std::move(points.value()));
}

bool PointsEqual(const BoundedLassoPoints::Snapshot& snapshot, std::span<const Vec2> expected)
{
    if (snapshot.Count() != expected.size())
        return false;
    std::vector<Vec2> actual;
    return snapshot.CopyToForWork(actual) && actual.size() == expected.size() &&
           std::equal(actual.begin(), actual.end(), expected.begin());
}

void CheckBeginAndCapturedAuthority()
{
    auto memory = SharedPublicationMemory::CreateForWork();
    auto gesture = NewGesture(memory);
    Expect(gesture.has_value(), "begin-fixture-has-accounted-point-owner");
    if (!gesture)
        return;
    const bool began = gesture->BeginOwner(17, LassoKind::Polygonal, SelectionMode::Add, false, {2.25, -3.5});
    const auto display = gesture->CaptureOwner();
    Expect(began && gesture->ActiveOwner() && gesture->CurrentOwner() == BoundedLassoGesture::Key{17, 1},
           "begin-opens-nonzero-key-at-revision-one");
    Expect(display.kind == LassoKind::Polygonal && display.mode == SelectionMode::Add && !display.antialiased &&
               !display.anchor && display.points.Count() == 1 &&
               PointsEqual(display.points, std::array<Vec2, 1>{{{2.25, -3.5}}}),
           "begin-captures-kind-mode-aa-and-literal-point");

    auto invalid = NewGesture(memory);
    Expect(invalid && !invalid->BeginOwner(0, LassoKind::Freehand, SelectionMode::New, true, {0, 0}) &&
               !invalid->BeginOwner(1, static_cast<LassoKind>(4), SelectionMode::New, true, {0, 0}) &&
               !invalid->BeginOwner(1, LassoKind::Freehand, static_cast<SelectionMode>(3), true, {0, 0}) &&
               !invalid->BeginOwner(1, LassoKind::Freehand, SelectionMode::New, true,
                                    {std::numeric_limits<double>::infinity(), 0}) &&
               !invalid->ActiveOwner() && invalid->CaptureOwner().points.Empty(),
           "zero-opening-invalid-kind-mode-and-nonfinite-begin-refuse");
}

void CheckExtensionThresholdAndLastPointBoundary()
{
    auto memory = SharedPublicationMemory::CreateForWork();
    auto gesture = NewGesture(memory, 2);
    Expect(gesture.has_value(), "extension-fixture-has-accounted-point-owner");
    if (!gesture)
        return;
    static_cast<void>(gesture->BeginOwner(21, LassoKind::Freehand, SelectionMode::New, true, {0, 0}));
    const auto key = gesture->CurrentOwner();
    const auto below = gesture->ExtendOwner(key, {0.149, 0.199});
    const bool unchanged = below == BoundedLassoGesture::Outcome::Unchanged && gesture->CurrentOwner() == key &&
                           gesture->PointCountOwner() == 1;
    const auto exact = gesture->ExtendOwner(key, {0.25, 0});
    const bool thresholdAccepted = exact == BoundedLassoGesture::Outcome::Changed &&
                                   gesture->CurrentOwner() == BoundedLassoGesture::Key{21, 2} &&
                                   gesture->PointCountOwner() == 2;
    Expect(unchanged && thresholdAccepted, "quarter-pixel-below-is-skipped-and-exact-distance-is-added");
    Expect(gesture->ExtendOwner(key, {4, 4}) == BoundedLassoGesture::Outcome::Refused &&
               gesture->ExtendOwner(gesture->CurrentOwner(), {std::numeric_limits<double>::quiet_NaN(), 1}) ==
                   BoundedLassoGesture::Outcome::Refused &&
               gesture->PointCountOwner() == 2 && gesture->CurrentOwner() == BoundedLassoGesture::Key{21, 2},
           "stale-key-and-nonfinite-extension-refuse-without-mutation");

    for (std::uint32_t index = 2; index < 64; ++index)
    {
        const auto current = gesture->CurrentOwner();
        static_cast<void>(gesture->ExtendOwner(current, {static_cast<double>(index), 3.0}));
    }
    const auto boundaryDisplay = gesture->CaptureOwner();
    const bool boundary =
        boundaryDisplay.points.Count() == 64 &&
        boundaryDisplay.points.Bounds() == std::optional<SelectionBounds>{SelectionBounds{0, 0, 63, 3}};
    // Independently observe the 64th point through a bounded copied sequence; do not use implementation internals.
    std::vector<Vec2> points64;
    const bool copied64 = boundaryDisplay.points.CopyToForWork(points64);
    Expect(boundary && copied64 && points64.size() == 64 && points64[0] == Vec2{0, 0} && points64[1] == Vec2{0.25, 0} &&
               points64[63] == Vec2{63, 3},
           "last-owner-semantics-cross-inline-to-sealed-64-point-boundary");
    const auto beforePop = gesture->CaptureOwner();
    static_cast<void>(gesture->PopOwner(gesture->CurrentOwner()));
    const auto afterPop = gesture->CaptureOwner();
    std::vector<Vec2> popped;
    const bool popCopied = afterPop.points.CopyToForWork(popped);
    Expect(popCopied && popped.size() == 63 && popped.back() == Vec2{62, 3} && PointsEqual(beforePop.points, points64),
           "pop-restores-last-point-and-prior-64-point-snapshot-remains-immutable");

    auto direct = BoundedLassoPoints::CreateForWork(memory, 1);
    bool filled = direct.has_value();
    if (direct)
    {
        for (std::uint32_t index = 0; index < 64; ++index)
            filled = direct->PushOwner({static_cast<double>(index), -static_cast<double>(index)}) && filled;
        const auto tailLast = direct->LastOwner();
        filled = direct->PushOwner({64, -64}) && filled;
        filled = direct->PopOwner() && filled;
        const auto sealedLast = direct->LastOwner();
        Expect(filled && tailLast == Vec2{63, -63} && sealedLast == Vec2{63, -63} && direct->CountOwner() == 64,
               "last-point-lookup-survives-64-point-seal-and-pop-to-sealed-prefix");
    }
    else
        Expect(false, "last-point-lookup-survives-64-point-seal-and-pop-to-sealed-prefix");
}

void CheckCursorFinishAndTerminalSnapshots()
{
    auto memory = SharedPublicationMemory::CreateForWork();
    auto gesture = NewGesture(memory);
    Expect(gesture.has_value(), "terminal-fixture-has-accounted-point-owner");
    if (!gesture)
        return;
    static_cast<void>(gesture->BeginOwner(31, LassoKind::Polygonal, SelectionMode::Subtract, false, {1, 2}));
    auto key = gesture->CurrentOwner();
    static_cast<void>(gesture->ExtendOwner(key, {8, 2}));
    key = gesture->CurrentOwner();
    static_cast<void>(gesture->ExtendOwner(key, {4, 9}));
    key = gesture->CurrentOwner();
    const auto cursorResult = gesture->CursorOwner(key, Vec2{30, 40});
    const auto withCursor = gesture->CaptureOwner();
    Expect(cursorResult == BoundedLassoGesture::Outcome::Changed && withCursor.cursor == Vec2{30, 40} &&
               withCursor.points.Count() == 3 && withCursor.key.revision == 4,
           "polygon-cursor-is-versioned-display-only-state");
    const auto finished = gesture->FinishOwner(gesture->CurrentOwner());
    const std::array<Vec2, 3> literalOutline{{{1, 2}, {8, 2}, {4, 9}}};
    Expect(finished && !finished->cursor && PointsEqual(finished->points, literalOutline),
           "finish-snapshot-excludes-rubber-band-cursor");
    Expect(!gesture->ActiveOwner() && gesture->PointCountOwner() == 0 &&
               PointsEqual(finished->points, literalOutline) &&
               gesture->ExtendOwner(finished->key, {12, 14}) == BoundedLassoGesture::Outcome::Refused,
           "finish-closes-writer-while-returned-snapshot-retains-outline");

    auto cancelled = NewGesture(memory);
    Expect(cancelled.has_value(), "cancel-fixture-has-accounted-point-owner");
    if (!cancelled)
        return;
    static_cast<void>(cancelled->BeginOwner(32, LassoKind::Freehand, SelectionMode::New, true, {-1, 5}));
    const auto retained = cancelled->CaptureOwner();
    Expect(cancelled->CancelOwner(cancelled->CurrentOwner()) && !cancelled->ActiveOwner() &&
               cancelled->PointCountOwner() == 0 && PointsEqual(retained.points, std::array<Vec2, 1>{{{-1, 5}}}),
           "cancel-closes-writer-and-preserves-precancel-display-snapshot");

    auto popping = NewGesture(memory);
    Expect(popping.has_value(), "empty-pop-fixture-has-accounted-point-owner");
    if (!popping)
        return;
    static_cast<void>(popping->BeginOwner(33, LassoKind::Freehand, SelectionMode::New, true, {7, 8}));
    const auto beforeLastPop = popping->CaptureOwner();
    Expect(popping->PopOwner(popping->CurrentOwner()) == BoundedLassoGesture::Outcome::Changed &&
               !popping->ActiveOwner() && popping->PointCountOwner() == 0 &&
               PointsEqual(beforeLastPop.points, std::array<Vec2, 1>{{{7, 8}}}),
           "backspace-empty-ends-draft-without-changing-captured-snapshot");
}

bool CheckMarqueeCase(LassoKind kind, Vec2 rawAnchor, Vec2 drag, bool square, bool fromCenter, SelectionBounds expected,
                      std::array<Vec2, 4> expectedCorners, const char* assertion)
{
    auto memory = SharedPublicationMemory::CreateForWork();
    auto gesture = NewGesture(memory);
    if (!gesture || !gesture->BeginOwner(41, kind, SelectionMode::New, true, rawAnchor))
    {
        Expect(false, assertion);
        return false;
    }
    const auto anchorDisplay = gesture->CaptureOwner();
    const bool anchorGood = anchorDisplay.anchor == Vec2{10, -3} && anchorDisplay.points.Count() == 1;
    const auto outcome = gesture->MarqueeOwner(gesture->CurrentOwner(), drag, square, fromCenter);
    const auto display = gesture->CaptureOwner();
    const bool good = anchorGood && outcome == BoundedLassoGesture::Outcome::Changed &&
                      display.anchor == Vec2{10, -3} && display.kind == kind &&
                      display.points.Bounds() == std::optional<SelectionBounds>{expected} &&
                      PointsEqual(display.points, expectedCorners);
    Expect(good, assertion);
    return good;
}

void CheckMarqueeGeometryAndDuplicate()
{
    static_cast<void>(CheckMarqueeCase(LassoKind::Rectangle, {10.4, -2.6}, {6.2, 1.2}, false, false, {6, -3, 10, 1},
                                       {{{6, -3}, {10, -3}, {10, 1}, {6, 1}}},
                                       "rectangle-rounds-anchor-and-emits-literal-reversed-corners"));
    static_cast<void>(CheckMarqueeCase(LassoKind::Rectangle, {10.4, -2.6}, {13.2, -1.1}, true, false, {10, -3, 13, 0},
                                       {{{10, -3}, {13, -3}, {13, 0}, {10, 0}}},
                                       "rectangle-square-uses-farthest-side-and-literal-corners"));
    static_cast<void>(CheckMarqueeCase(LassoKind::Rectangle, {10.4, -2.6}, {12.3, -5.4}, false, true, {8, -5, 12, -1},
                                       {{{8, -5}, {12, -5}, {12, -1}, {8, -1}}},
                                       "rectangle-from-center-grows-around-rounded-anchor"));
    static_cast<void>(CheckMarqueeCase(LassoKind::Ellipse, {10.4, -2.6}, {6.2, 1.2}, false, false, {6, -3, 10, 1},
                                       {{{6, -3}, {10, -3}, {10, 1}, {6, 1}}},
                                       "ellipse-uses-whole-pixel-box-and-four-literal-corners"));
    static_cast<void>(CheckMarqueeCase(LassoKind::Ellipse, {10.4, -2.6}, {13.2, -1.1}, true, false, {10, -3, 13, 0},
                                       {{{10, -3}, {13, -3}, {13, 0}, {10, 0}}},
                                       "ellipse-square-uses-independent-literal-bounds"));
    static_cast<void>(CheckMarqueeCase(LassoKind::Ellipse, {10.4, -2.6}, {12.3, -5.4}, false, true, {8, -5, 12, -1},
                                       {{{8, -5}, {12, -5}, {12, -1}, {8, -1}}},
                                       "ellipse-from-center-uses-independent-literal-bounds"));

    auto memory = SharedPublicationMemory::CreateForWork();
    auto gesture = NewGesture(memory);
    Expect(gesture.has_value(), "duplicate-marquee-fixture-has-accounted-point-owner");
    if (!gesture)
        return;
    static_cast<void>(gesture->BeginOwner(42, LassoKind::Rectangle, SelectionMode::New, true, {3.2, 4.2}));
    const auto key = gesture->CurrentOwner();
    const auto first = gesture->MarqueeOwner(key, {8.1, 9.1}, false, false);
    const auto changedKey = gesture->CurrentOwner();
    const auto duplicate = gesture->MarqueeOwner(changedKey, {8.2, 8.8}, false, false);
    Expect(first == BoundedLassoGesture::Outcome::Changed && duplicate == BoundedLassoGesture::Outcome::Unchanged &&
               gesture->CurrentOwner() == changedKey && changedKey.revision == key.revision + 1 &&
               gesture->PointCountOwner() == 4,
           "duplicate-marquee-box-does-not-increment-opening-epoch");
    Expect(gesture->ExtendOwner(changedKey, {11, 12}) == BoundedLassoGesture::Outcome::Refused &&
               gesture->CursorOwner(changedKey, Vec2{4, 5}) == BoundedLassoGesture::Outcome::Refused &&
               gesture->MarqueeOwner({changedKey.opening + 1, changedKey.revision}, {9, 9}, false, false) ==
                   BoundedLassoGesture::Outcome::Refused,
           "wrong-kind-extension-and-stale-marquee-key-refuse");
}

void CheckRevisionExhaustionAndMove()
{
    auto memory = SharedPublicationMemory::CreateForWork();
    auto gesture = NewGesture(memory);
    Expect(gesture.has_value(), "revision-fixture-has-accounted-point-owner");
    if (!gesture)
        return;
    static_cast<void>(gesture->BeginOwner(51, LassoKind::Polygonal, SelectionMode::New, true, {1, 1}));
    static_cast<void>(gesture->CursorOwner(gesture->CurrentOwner(), Vec2{3, 4}));
    const bool setMax = gesture->SetRevisionForTest((std::numeric_limits<std::uint64_t>::max)());
    const auto maxKey = gesture->CurrentOwner();
    const auto extension = gesture->ExtendOwner(maxKey, {2, 2});
    const auto cursor = gesture->CursorOwner(maxKey, std::nullopt);
    const auto finish = gesture->FinishOwner(maxKey);
    Expect(setMax && extension == BoundedLassoGesture::Outcome::Refused &&
               cursor == BoundedLassoGesture::Outcome::Refused && !finish && gesture->ActiveOwner() &&
               gesture->CurrentOwner() == maxKey && gesture->PointCountOwner() == 1,
           "max-revision-refuses-versioned-mutations-and-finish-without-wrapping");
    Expect(gesture->CancelOwner(maxKey) && !gesture->ActiveOwner() && gesture->PointCountOwner() == 0,
           "max-revision-cancel-still-closes-without-increment");

    auto source = NewGesture(memory);
    Expect(source.has_value(), "move-fixture-has-accounted-point-owner");
    if (!source)
        return;
    static_cast<void>(source->BeginOwner(52, LassoKind::Freehand, SelectionMode::Subtract, false, {-5, 6}));
    BoundedLassoGesture moved(std::move(source.value()));
    const auto movedFrom = source->CaptureOwner();
    const auto movedTo = moved.CaptureOwner();
    Expect(!source->ActiveOwner() && source->PointCountOwner() == 0 && movedFrom.points.Empty() &&
               movedFrom.key == BoundedLassoGesture::Key{} && moved.ActiveOwner() && movedTo.key.opening == 52 &&
               movedTo.points.Count() == 1 && movedTo.mode == SelectionMode::Subtract && !movedTo.antialiased,
           "moved-from-gesture-is-inactive-and-moved-to-retains-captured-state");
}
} // namespace

int RunBoundedLassoGestureTests()
{
    gAssertions = 0;
    gFailures = 0;
    CheckBeginAndCapturedAuthority();
    CheckExtensionThresholdAndLastPointBoundary();
    CheckCursorFinishAndTerminalSnapshots();
    CheckMarqueeGeometryAndDuplicate();
    CheckRevisionExhaustionAndMove();
    std::printf("%s bounded-lasso-gesture.summary checks=%d failures=%d\n", gFailures == 0 ? "PASS" : "FAIL",
                gAssertions, gFailures);
    return gFailures;
}
