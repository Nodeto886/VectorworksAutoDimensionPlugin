#include "vwad/AutoDimensionAlgorithms.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace {

using namespace vwad::algo;

int failures = 0;

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

bool near(double a, double b, double epsilon = 1e-7)
{
    return std::abs(a - b) <= epsilon;
}

Point2 rotate(const Point2& point, double angle)
{
    return {
        point.x * std::cos(angle) - point.y * std::sin(angle),
        point.x * std::sin(angle) + point.y * std::cos(angle),
    };
}

void testScaleAwareTolerance()
{
    const TolerancePolicy tolerance{1e-6, 1e-8, 1e-8};
    expect(near(tolerance.linear(1.0), 1e-6), "absolute tolerance wins at small scale");
    expect(near(tolerance.linear(1e6), 1e-2), "relative tolerance wins at large scale");
}

void testWorldSpaceDimensionAlignment()
{
    const Segment2 reference{{0.0, 0.0}, {10.0, 0.0}};
    double offset = 0.0;
    expect(alignedDimensionOffset(reference, 100.0, {{0.0, 50.0}, {10.0, 50.0}}, offset),
        "parallel dimensions can be aligned");
    expect(near(offset, 50.0), "candidate offset is measured from its own start point");
    expect(alignedDimensionOffset(reference, 100.0, {{10.0, 50.0}, {0.0, 50.0}}, offset),
        "reversed endpoint dimensions can be aligned");
    expect(near(offset, -50.0), "reversed endpoint order flips the required offset sign");
    expect(!alignedDimensionOffset(reference, 100.0, {{0.0, 0.0}, {0.0, 10.0}}, offset),
        "perpendicular dimensions are not silently aligned");
}

void testDominantAxisWithoutBinDiscontinuity()
{
    const double angle = 7.3 * kPi / 180.0;
    const Point2 u{std::cos(angle), std::sin(angle)};
    const Point2 v{-u.y, u.x};
    std::vector<Segment2> segments = {
        {{0.0, 0.0}, u * 20.0},
        {u * 20.0, u * 20.0 + v * 10.0},
        {u * 20.0 + v * 10.0, v * 10.0},
        {v * 10.0, {0.0, 0.0}},
        {{3.0, 2.0}, {3.01, 2.04}}, // short noise edge
    };
    const DominantAxis axis = findDominantAxis(segments);
    expect(axis.valid, "dominant axis is found");
    expect(std::abs(std::abs(dot(axis.direction, u)) - 1.0) < 1e-5, "dominant axis follows rotated rectangle");
    expect(axis.confidence > 0.95, "orthogonal frame has high confidence");
    expect(std::abs(axis.foldedAngleDegrees - 7.3) < 0.02, "folded angle is continuous despite short noisy edges");
}

void testMinimumAreaBounds()
{
    const double angle = 31.0 * kPi / 180.0;
    std::vector<Point2> points;
    for (Point2 point : std::vector<Point2>{{-5.0, -2.0}, {5.0, -2.0}, {5.0, 2.0}, {-5.0, 2.0}, {0.0, 0.0}}) {
        point = rotate(point, angle);
        point = point + Point2{100.0, -50.0};
        points.push_back(point);
    }
    const OrientedBounds bounds = minimumAreaBounds(points);
    expect(bounds.valid, "minimum-area bounds are valid");
    const std::vector<double> dimensions = {bounds.width, bounds.height};
    expect((near(dimensions[0], 10.0, 1e-6) && near(dimensions[1], 4.0, 1e-6)) ||
        (near(dimensions[0], 4.0, 1e-6) && near(dimensions[1], 10.0, 1e-6)),
        "minimum-area bounds recover true dimensions");
    expect(near(bounds.area, 40.0, 1e-5), "minimum-area bounds recover true area");
}

void testSpatialPointIndex()
{
    SpatialPointIndex index(0.01);
    const std::size_t first = index.insertOrFind({0.0, 0.0});
    const std::size_t duplicateAcrossCell = index.insertOrFind({-0.004, 0.003});
    const std::size_t distinct = index.insertOrFind({0.02, 0.0});
    expect(first == duplicateAcrossCell, "spatial hash merges neighboring-cell point");
    expect(first != distinct, "spatial hash preserves distinct point");
}

void testTopologyChainOrder()
{
    const std::vector<Segment2> shuffled = {
        {{2.0, 1.0}, {3.0, 1.0}},
        {{0.0, 0.0}, {1.0, 0.0}},
        {{1.0, 0.0}, {2.0, 1.0}},
    };
    const auto chains = orderSegmentChains(shuffled);
    expect(chains.size() == 1, "connected segments form one chain");
    expect(chains.front().size() == 3, "all connected segments are retained");
    for (std::size_t index = 1; index < chains.front().size(); ++index) {
        expect(length(chains.front()[index - 1].end - chains.front()[index].start) < 1e-9,
            "chain edges are endpoint-contiguous");
    }
}

void testRobustSegmentIntersection()
{
    Point2 hit;
    double parameter = -1.0;
    expect(segmentIntersection({{0.0, 0.0}, {10.0, 0.0}}, {{5.0, -1.0}, {5.0, 1.0}}, hit, parameter),
        "perpendicular segments intersect");
    expect(near(hit.x, 5.0) && near(hit.y, 0.0) && near(parameter, 0.5), "intersection coordinates and parameter");
    expect(!segmentIntersection({{0.0, 0.0}, {1e6, 1.0}}, {{0.0, 1.0}, {1e6, 2.0}}, hit, parameter),
        "large nearly parallel segments are rejected by angular tolerance");
}

void testEllipseAndSemicircleIntersections()
{
    const Segment2 query{{-10.0, 0.0}, {10.0, 0.0}};
    const auto ellipse = segmentEllipseIntersections(query, {0.0, 0.0}, 5.0, 2.0, 0.0, 0.0, 2.0 * kPi);
    expect(ellipse.size() == 2, "line intersects ellipse twice");
    if (ellipse.size() == 2) {
        expect(near(ellipse[0].second.x, -5.0) && near(ellipse[1].second.x, 5.0), "ellipse intersections are exact");
    }

    const Segment2 vertical{{0.0, -10.0}, {0.0, 10.0}};
    const auto upperSemicircle = segmentEllipseIntersections(vertical, {0.0, 0.0}, 5.0, 5.0, 0.0, 0.0, kPi);
    expect(upperSemicircle.size() == 1, "arc sweep filters the lower circle hit");
    if (!upperSemicircle.empty()) expect(near(upperSemicircle[0].second.y, 5.0), "semicircle endpoint geometry remains measurable");

    const auto largeScaleMiss = segmentEllipseIntersections(
        {{-2.0e9, 1.0e9 + 100.0}, {2.0e9, 1.0e9 + 100.0}},
        {0.0, 0.0},
        1.0e9,
        1.0e9,
        0.0,
        0.0,
        2.0 * kPi);
    expect(largeScaleMiss.empty(), "large-scale line outside ellipse is not clamped to a false tangent");
}

void testIntervalLaneAssignment()
{
    const std::vector<std::size_t> lanes = assignIntervalLanes({
        {0.0, 10.0, 0},
        {5.0, 12.0, 1},
        {13.0, 20.0, 2},
    }, 0.0);
    expect(lanes.size() == 3, "one lane result per interval");
    expect(lanes[0] != lanes[1], "overlapping intervals use different lanes");
    expect(lanes[0] == lanes[2], "non-overlapping intervals reuse a lane");
}

void testOrthogonalSpanningTree()
{
    const std::vector<Point2> points = {{0.0, 0.0}, {10.0, 0.2}, {20.0, 0.0}, {10.0, 10.0}};
    const auto edges = orthogonalSpanningTree(points);
    expect(edges.size() == points.size() - 1, "center graph is a spanning tree");
    bool hasLongDiagonal = false;
    for (const auto& edge : edges) {
        const Point2 delta = points[edge.first] - points[edge.second];
        if (std::abs(delta.x) > 9.0 && std::abs(delta.y) > 9.0) hasLongDiagonal = true;
    }
    expect(!hasLongDiagonal, "orthogonal tree avoids unnecessary diagonal links");

    std::vector<Point2> rotatedSquare;
    for (Point2 point : std::vector<Point2>{{-5.0, -5.0}, {5.0, -5.0}, {5.0, 5.0}, {-5.0, 5.0}}) {
        rotatedSquare.push_back(rotate(point, 37.0 * kPi / 180.0));
    }
    const auto rotatedEdges = orthogonalSpanningTree(rotatedSquare);
    bool rotatedHasDiagonal = false;
    for (const auto& edge : rotatedEdges) {
        const double edgeLength = length(rotatedSquare[edge.first] - rotatedSquare[edge.second]);
        if (edgeLength > 11.0) rotatedHasDiagonal = true;
    }
    expect(!rotatedHasDiagonal, "symmetric rotated grid recovers a local axis instead of world X/Y");
}

void testGlobalLabelLayout()
{
    const std::vector<LabelInput> labels = {
        {{0.0, 0.0, 10.0, 4.0}, {1.0, 0.0}, 6.0, 5.0, 1.0},
        {{2.0, 0.0, 12.0, 4.0}, {1.0, 0.0}, 6.0, 5.0, 1.0},
        {{4.0, 0.0, 14.0, 4.0}, {1.0, 0.0}, 6.0, 5.0, 1.0},
    };
    const std::vector<LabelBox> obstacles = {{-1.0, -1.0, 15.0, 4.5}};
    const auto placements = optimizeLabelLayout(labels, obstacles, 2, 3, 0.1);
    expect(placements.size() == labels.size(), "layout returns every label");
    for (const LabelPlacement& placement : placements) {
        expect(overlapArea(placement.box, obstacles.front(), 0.1) == 0.0, "labels move outside source obstacle");
    }
    for (std::size_t first = 0; first < placements.size(); ++first) {
        for (std::size_t second = first + 1; second < placements.size(); ++second) {
            expect(overlapArea(placements[first].box, placements[second].box, 0.1) == 0.0,
                "optimized labels do not overlap each other");
        }
    }
}

void testWeightedSetCover()
{
    const std::vector<CoverageCandidate> candidates = {
        {{0, 1}, 1.0, 0},
        {{1, 2}, 1.0, 1},
        {{0, 1, 2}, 3.0, 2},
    };
    const auto selected = selectCoveringCandidates(3, candidates);
    expect(selected.size() == 2, "weighted cover chooses two efficient dimensions");
    expect(std::find(selected.begin(), selected.end(), 0) != selected.end(), "cover includes first efficient candidate");
    expect(std::find(selected.begin(), selected.end(), 1) != selected.end(), "cover includes second efficient candidate");
}

void testRepresentativeSegmentSelection()
{
    const std::vector<Segment2> segments = {
        {{0.0, 0.0}, {20.0, 0.0}},
        {{20.0, 0.0}, {20.0, 10.0}},
        {{20.0, 10.0}, {19.0, 10.0}},
        {{19.0, 10.0}, {19.0, 9.0}},
    };
    const auto selected = selectRepresentativeSegments(segments, 2);
    expect(selected.size() == 2, "representative segment selection respects output cap");
    expect(std::find(selected.begin(), selected.end(), 0) != selected.end(), "representative selection keeps long horizontal edge");
    expect(std::find(selected.begin(), selected.end(), 1) != selected.end(), "representative selection covers vertical direction family");
}

void testTransformInvariance()
{
    const std::vector<Point2> base = {{-3.0, -1.0}, {3.0, -1.0}, {3.0, 1.0}, {-3.0, 1.0}};
    std::vector<Point2> transformed;
    for (Point2 point : base) transformed.push_back(rotate(point * 1000.0, 0.37) + Point2{1e8, -2e8});
    const OrientedBounds bounds = minimumAreaBounds(transformed);
    expect(bounds.valid, "large translated geometry remains valid");
    expect(near(bounds.area, 12.0e6, 1.0), "minimum-area bounds are scale/translation invariant");
}

void testAxisClassification()
{
    expect(isAxisVertical(0.0, 1.0), "pure vertical axis is classified vertical");
    expect(isAxisVertical(-1e-12, 5.0), "tiny horizontal component is still vertical");
    expect(!isAxisVertical(1.0, 0.0), "pure horizontal axis is not vertical");
    expect(!isAxisVertical(std::sqrt(0.5), std::sqrt(0.5)), "diagonal axis is not vertical");
}

void testOrthoVerticalAlignOffset()
{
    // Vectorworks measures a constrained (ortho) vertical dimension with a positive start
    // offset to the RIGHT, while the unified candidate normal points LEFT for a vertical
    // axis. Aligning a vertical ortho dimension must flip the sign so it lands on the
    // clicked side instead of being mirrored across the witness origin.
    expect(near(orthoVerticalAlignOffset(10.0, 0, 0.0, 1.0), -10.0),
        "vertical ortho align flips a positive offset sign");
    expect(near(orthoVerticalAlignOffset(-4.0, 0, 0.0, -1.0), 4.0),
        "vertical ortho align flips a negative offset sign");
    // Horizontal ortho keeps the derived offset (normal points up, matching convention).
    expect(near(orthoVerticalAlignOffset(10.0, 0, 1.0, 0.0), 10.0),
        "horizontal ortho align keeps the offset sign");
    // Aligned dimensions use the geometry normal and must not be flipped here.
    expect(near(orthoVerticalAlignOffset(10.0, 1, 0.0, 1.0), 10.0),
        "aligned vertical align keeps the offset sign");
    expect(near(orthoVerticalAlignOffset(10.0, 1, 1.0, 0.0), 10.0),
        "aligned horizontal align keeps the offset sign");
}

void testReversedDimensionOffset()
{
    // Reversing the endpoints of an ALIGNED dimension flips its normal (-dy,dx)/L, so the
    // offset must be negated to keep the line on the same side. Ortho (corner) dimensions
    // use an axis-fixed normal (up for horizontal, right for vertical) that does not depend
    // on endpoint order, so their offset is left unchanged; negating would mirror the line
    // to the opposite side.
    expect(near(reversedDimensionOffset(8.0, 1), -8.0),
        "reversing aligned endpoints negates the offset");
    expect(near(reversedDimensionOffset(-3.0, 1), 3.0),
        "reversing aligned endpoints with negative offset flips to positive");
    expect(near(reversedDimensionOffset(8.0, 0), 8.0),
        "reversing ortho (vertical or horizontal) endpoints keeps the offset sign");
    expect(near(reversedDimensionOffset(-3.0, 0), -3.0),
        "reversing ortho endpoints with negative offset keeps the sign");
}

void testVerticalCornerAlignConversion()
{
    // The alignment path derives a target offset through the geometry normal
    // (-directionY, directionX) and then, for a constrained vertical dimension, flips it
    // to Vectorworks' right-positive convention. Verify the flip is the only difference
    // between the derived value and the offset handed to CreateLinearReplacement.
    const Segment2 reference{{0.0, 0.0}, {0.0, 10.0}}; // clicked vertical reference through origin
    const Segment2 candidate{{-3.0, 5.0}, {-3.0, -5.0}}; // vertical ortho dimension, 3 units left
    double derived = 0.0;
    expect(alignedDimensionOffset(reference, 0.0, candidate, derived),
        "vertical candidate derives an aligned offset");
    // Without the flip the rebuild offset would equal the derived value; the vertical
    // ortho convention requires the negation.
    expect(near(orthoVerticalAlignOffset(derived, 0, 0.0, 1.0), -derived),
        "vertical ortho align negates the derived offset to VW's right-positive convention");
    expect(near(orthoVerticalAlignOffset(derived, 0, 1.0, 0.0), derived),
        "horizontal ortho align preserves the derived offset");
}

} // namespace

int main()
{
    testScaleAwareTolerance();
    testWorldSpaceDimensionAlignment();
    testAxisClassification();
    testOrthoVerticalAlignOffset();
    testReversedDimensionOffset();
    testVerticalCornerAlignConversion();
    testDominantAxisWithoutBinDiscontinuity();
    testMinimumAreaBounds();
    testSpatialPointIndex();
    testTopologyChainOrder();
    testRobustSegmentIntersection();
    testEllipseAndSemicircleIntersections();
    testIntervalLaneAssignment();
    testOrthogonalSpanningTree();
    testGlobalLabelLayout();
    testWeightedSetCover();
    testRepresentativeSegmentSelection();
    testTransformInvariance();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "All AutoDimensionAlgorithms tests passed\n";
    return 0;
}
