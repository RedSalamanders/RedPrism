#pragma once

// Sidecar fixture data only. No boolean adapter/API is assumed or tested here.
// Source anchors are repeated in WandBooleanTestCases-r1-agent.md.
namespace WandBooleanTestCasesR1
{
struct Rect
{
    double x;
    double y;
    double width;
    double height;
};

struct Point
{
    double x;
    double y;
};

struct PixelProbe
{
    int x;
    int y;
    int expectedCoverage; // hard-edge coverage: 0 or 255
};

// Independent analytical shared-edge reference: interiors are disjoint and the common boundary has zero area.
inline constexpr Rect kSharedEdgeA{0, 0, 2, 2};
inline constexpr Rect kSharedEdgeB{2, 0, 2, 2};
inline constexpr double kSharedEdgeUnionArea = 8;
inline constexpr PixelProbe kSharedEdgeProbes[]{{0, 0, 255}, {1, 1, 255}, {2, 1, 255}, {4, 1, 0}};

// Independent analytical reference: two 10x10 rectangles overlap in a 5x5 square.
// Areas: A=100, B=100, intersection=25, union=175, A\B=75.
inline constexpr Rect kOverlapA{0, 0, 10, 10};
inline constexpr Rect kOverlapB{5, 5, 10, 10};
inline constexpr double kOverlapArea = 25;
inline constexpr double kUnionArea = 175;
inline constexpr double kSubtractArea = 75;
inline constexpr PixelProbe kOverlapUnionProbes[]{{2, 2, 255}, {7, 7, 255}, {14, 14, 255}, {15, 15, 0}};
inline constexpr PixelProbe kOverlapSubtractProbes[]{{2, 2, 255}, {7, 7, 0}, {9, 9, 0}, {10, 10, 0}};
inline constexpr PixelProbe kIntersectionProbes[]{{5, 5, 255}, {9, 9, 255}, {10, 10, 0}};

// Legacy SelectionTests.swift:32-41 / C++ port Document.Tests.Runner.cpp:2115-2129.
// Exact legacy lasso rectangles and checked pixels; areas below are independent calculations from those inputs.
inline constexpr Rect kLegacyReplace{10, 10, 40, 40};
inline constexpr Rect kLegacyAddCornerTouch{50, 50, 40, 40};
inline constexpr Rect kLegacySubtractContained{20, 20, 20, 20};
inline constexpr double kLegacyReplaceArea = 1600;
inline constexpr double kLegacyAfterAddArea = 3200; // disjoint interiors, one shared corner
inline constexpr double kLegacyAfterSubtractArea = 2800;
inline constexpr PixelProbe kLegacyAddProbes[]{{30, 30, 255}, {70, 70, 255}};
inline constexpr PixelProbe kLegacySubtractProbes[]{{30, 30, 0}, {15, 15, 255}};

// Legacy MagicWandTests.swift:80-89. Mask dimensions are 8x6; selected linear
// indices are expanded literally from the legacy 3x3 ring plus its two diagonal pixels.
// This is an independent expected list, not a test-time derivation from actual output.
inline constexpr int kTouchingMaskWidth = 8;
inline constexpr int kTouchingMaskHeight = 6;
inline constexpr int kTouchingMaskSelectedIndices[]{0, 1, 2, 8, 10, 16, 17, 18, 37, 46};
inline constexpr double kTouchingMaskSelectedArea = 10;

// Winding-hole analytical fixture: outer [0,3)x[0,3), inner [1,2)x[1,2)
// has opposite winding. The expected selected pixel set is the 3x3 ring (area 8).
// Numeric hole-outline provenance: legacy MagicWandTests.swift:80-89 describes the same 3x3 ring.
inline constexpr Rect kWindingHoleOuter{0, 0, 3, 3};
inline constexpr Point kWindingHoleOuterLoop[]{{0, 0}, {3, 0}, {3, 3}, {0, 3}};
inline constexpr Point kWindingHoleInnerLoop[]{{1, 1}, {1, 2}, {2, 2}, {2, 1}};
inline constexpr double kWindingHoleArea = 8;
inline constexpr int kWindingHoleSelectedIndices[]{0, 1, 2, 3, 5, 6, 7, 8};
inline constexpr PixelProbe kWindingHoleProbes[]{{0, 0, 255}, {1, 1, 0}, {2, 2, 255}};

// Fractional legacy input: SelectionTests.swift:309-315. The Marquee snaps the
// endpoints to integer bounds x=[20,60], y=[30,71], area=1640; this is a snapped
// marquee reference, not a claim that the legacy boolean operation accepts fractions unchanged.
inline constexpr double kLegacyMarqueeStartX = 60.4;
inline constexpr double kLegacyMarqueeStartY = 70.6;
inline constexpr double kLegacyMarqueeEndX = 20.2;
inline constexpr double kLegacyMarqueeEndY = 30.3;
inline constexpr Rect kLegacyMarqueeSnapped{20, 30, 40, 41};
inline constexpr double kLegacyMarqueeArea = 1640;
inline constexpr PixelProbe kLegacyMarqueeProbes[]{{20, 30, 255}, {19, 30, 0}};

// Fresh analytical fractional-geometry reference for a future adapter: exact area is 3.
// Under pixel-center hard-edge membership on a 4x3 canvas, indices 0, 1, 4, and 5 are selected.
inline constexpr Rect kAnalyticalFractionalRect{0.25, 0.25, 2.0, 1.5};
inline constexpr double kAnalyticalFractionalArea = 3;
inline constexpr int kAnalyticalFractionalSelectedIndices[]{0, 1, 4, 5};
} // namespace WandBooleanTestCasesR1
