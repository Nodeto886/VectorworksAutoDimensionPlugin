#include "vwad/AutoDimensionAlgorithms.h"

// Randomized property/metamorphic tests for the SDK-independent algorithm core.
//
// Unlike AutoDimensionAlgorithmsTests.cpp (hand-picked cases), this file checks
// mathematical invariants and metamorphic relations over many seeded random
// inputs so that a regression cannot hide behind the exact cases we happened to
// think of:
//
//   * convex hull: convex, contains every input point
//   * min-area oriented bounds: contains every input point, area >= hull area,
//     rotation/scale/translation equivariance
//   * dominant axis: unit direction, folded angle in [0,45], rotation equivariance
//   * segment intersection: returned point lies on both segments, symmetric
//   * ellipse/arc intersection: points on the ellipse and on the query segment
//   * interval lanes: same-lane intervals are disjoint past the gap
//   * chains: every non-degenerate segment used exactly once, edges contiguous
//   * set cover: every coverable feature is covered, candidates are distinct
//   * label layout: translation equivariance, bounded movement
//   * tolerance policy: monotone in scale
//   * robustness: degenerate / extreme inputs never crash and never produce NaN
//
// The seed is fixed so CI output is reproducible. The test is self-contained
// (stdlib only), matching the zero-dependency policy of the rest of the suite.

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <random>
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

bool near(double a, double b, double epsilon = 1e-9)
{
    return std::abs(a - b) <= epsilon;
}

bool finite(const Point2& p)
{
    return std::isfinite(p.x) && std::isfinite(p.y);
}

double cross3(const Point2& a, const Point2& b, const Point2& c)
{
    return cross(b - a, c - a);
}

double distanceToSegment(const Point2& p, const Segment2& s)
{
    const Point2 d = s.end - s.start;
    const double length = std::hypot(d.x, d.y);
    if (length == 0.0) return std::hypot(p.x - s.start.x, p.y - s.start.y);
    const double t = std::clamp(dot(p - s.start, d) / (length * length), 0.0, 1.0);
    return std::hypot(p.x - (s.start.x + d.x * t), p.y - (s.start.y + d.y * t));
}

double signedPolygonArea(const std::vector<Point2>& polygon)
{
    double area = 0.0;
    for (std::size_t i = 0; i < polygon.size(); ++i) {
        const Point2 a = polygon[i];
        const Point2 b = polygon[(i + 1) % polygon.size()];
        area += a.x * b.y - b.x * a.y;
    }
    return area * 0.5;
}

Point2 rotate(const Point2& point, double angle)
{
    return {
        point.x * std::cos(angle) - point.y * std::sin(angle),
        point.x * std::sin(angle) + point.y * std::cos(angle),
    };
}

// ---------------------------------------------------------------- utilities

std::vector<Point2> randomPoints(std::mt19937_64& rng, std::size_t count)
{
    std::uniform_real_distribution<double> unit(-100.0, 100.0);
    std::vector<Point2> points;
    points.reserve(count);
    for (std::size_t i = 0; i < count; ++i) points.push_back({unit(rng), unit(rng)});
    return points;
}

std::vector<Segment2> randomSegments(std::mt19937_64& rng, std::size_t count)
{
    std::uniform_real_distribution<double> unit(-100.0, 100.0);
    std::vector<Segment2> segments;
    segments.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        segments.push_back({{unit(rng), unit(rng)}, {unit(rng), unit(rng)}});
    }
    return segments;
}

// --------------------------------------------------------- convex hull props

void propertyConvexHullContainsAllPoints()
{
    std::mt19937_64 rng(0xC0FFEE1);
    for (int iteration = 0; iteration < 200; ++iteration) {
        const std::vector<Point2> points = randomPoints(rng, 2 + rng() % 60);
        const std::vector<Point2> hull = convexHull(points);
        expect(!hull.empty(), "hull is never empty for non-empty input");
        if (hull.size() < 3) {
            // Collinear / degenerate sets collapse to a segment or a point.
            continue;
        }
        const double polygonArea = signedPolygonArea(hull);
        expect(polygonArea > -1e-9, "hull is positively oriented (counter-clockwise)");
        const double scale = geometryScale(points);
        const double epsilon = std::max(1e-6, 1e-9 * std::max(1.0, scale));
        for (const Point2& point : points) {
            bool inside = true;
            for (std::size_t edge = 0; edge < hull.size(); ++edge) {
                if (cross3(hull[edge], hull[(edge + 1) % hull.size()], point) < -epsilon) {
                    inside = false;
                    break;
                }
            }
            expect(inside, "every input point lies inside or on the convex hull");
        }
    }
}

void propertyConvexHullIsConvex()
{
    std::mt19937_64 rng(0xC0FFEE2);
    for (int iteration = 0; iteration < 200; ++iteration) {
        const std::vector<Point2> points = randomPoints(rng, 4 + rng() % 60);
        const std::vector<Point2> hull = convexHull(points);
        if (hull.size() < 3) continue;
        const double scale = geometryScale(points);
        const double epsilon = std::max(1e-6, 1e-9 * std::max(1.0, scale));
        for (std::size_t i = 0; i < hull.size(); ++i) {
            const Point2 a = hull[i];
            const Point2 b = hull[(i + 1) % hull.size()];
            const Point2 c = hull[(i + 2) % hull.size()];
            expect(cross3(a, b, c) >= -epsilon, "hull has only convex turns");
        }
    }
}

// ----------------------------------------- min-area oriented bounds props

void propertyMinAreaBoundsContainsAllPoints()
{
    std::mt19937_64 rng(0xC0FFEE3);
    for (int iteration = 0; iteration < 200; ++iteration) {
        const std::vector<Point2> points = randomPoints(rng, 3 + rng() % 60);
        const OrientedBounds bounds = minimumAreaBounds(points);
        if (!bounds.valid) continue;
        const double scale = geometryScale(points);
        const double epsilon = std::max(1e-6, 1e-9 * std::max(1.0, scale));
        expect(std::isfinite(bounds.area) && bounds.area >= 0.0, "bounds area is finite and non-negative");
        expect(std::isfinite(bounds.width) && std::isfinite(bounds.height), "bounds dimensions are finite");
        const double minU = dot(bounds.widthStart, bounds.axis);
        const double maxU = dot(bounds.widthEnd, bounds.axis);
        const double minV = dot(bounds.widthStart, bounds.normal);
        const double maxV = dot(bounds.heightEnd, bounds.normal);
        for (const Point2& point : points) {
            const double u = dot(point, bounds.axis);
            const double v = dot(point, bounds.normal);
            expect(u >= minU - epsilon && u <= maxU + epsilon, "point projects inside the box along the width axis");
            expect(v >= minV - epsilon && v <= maxV + epsilon, "point projects inside the box along the height axis");
        }
        const double hullArea = std::abs(signedPolygonArea(convexHull(points)));
        expect(bounds.area >= hullArea - epsilon, "bounding box area is at least the convex hull area");
    }
}

void propertyMinAreaBoundsEquivariance()
{
    std::mt19937_64 rng(0xC0FFEE4);
    for (int iteration = 0; iteration < 150; ++iteration) {
        const std::vector<Point2> base = randomPoints(rng, 3 + rng() % 40);
        const OrientedBounds original = minimumAreaBounds(base);
        if (!original.valid) continue;

        const double angle = std::uniform_real_distribution<double>(-3.14, 3.14)(rng);
        std::vector<Point2> rotated;
        rotated.reserve(base.size());
        for (const Point2& p : base) rotated.push_back(rotate(p, angle));
        const OrientedBounds rotatedBounds = minimumAreaBounds(rotated);
        if (rotatedBounds.valid) {
            expect(near(rotatedBounds.area, original.area, 1e-6 * std::max(1.0, original.area)),
                "rotating the points rotates the optimal box and preserves its area");
        }

        const double scale = std::uniform_real_distribution<double>(0.5, 10.0)(rng);
        const Point2 offset{std::uniform_real_distribution<double>(-1e4, 1e4)(rng),
            std::uniform_real_distribution<double>(-1e4, 1e4)(rng)};
        std::vector<Point2> transformed;
        transformed.reserve(base.size());
        for (const Point2& p : base) transformed.push_back(p * scale + offset);
        const OrientedBounds scaledBounds = minimumAreaBounds(transformed);
        if (scaledBounds.valid) {
            const double areaScale = scale * scale;
            expect(near(scaledBounds.area, original.area * areaScale, 1e-6 * std::max(1.0, original.area * areaScale)),
                "uniform scaling scales the bounding box area by scale^2");
        }
    }
}

// ------------------------------------------------------- dominant axis props

void propertyDominantAxisInvariants()
{
    std::mt19937_64 rng(0xC0FFEE5);
    for (int iteration = 0; iteration < 200; ++iteration) {
        const std::vector<Segment2> segments = randomSegments(rng, 1 + rng() % 50);
        const DominantAxis axis = findDominantAxis(segments);
        if (!axis.valid) continue;
        expect(near(length(axis.direction), 1.0, 1e-9), "dominant axis direction is unit length");
        expect(axis.confidence >= 0.0 && axis.confidence <= 1.0, "dominant axis confidence is in [0,1]");
        expect(axis.foldedAngleDegrees >= 0.0 && axis.foldedAngleDegrees <= 45.0,
            "folded angle is folded into [0,45] degrees");
    }
}

void propertyDominantAxisRotationEquivariance()
{
    std::mt19937_64 rng(0xC0FFEE6);
    for (int iteration = 0; iteration < 100; ++iteration) {
        const std::vector<Segment2> base = randomSegments(rng, 2 + rng() % 30);
        const DominantAxis original = findDominantAxis(base);
        if (!original.valid) continue;
        const double angle = std::uniform_real_distribution<double>(-1.5, 1.5)(rng);
        std::vector<Segment2> rotated;
        rotated.reserve(base.size());
        for (const Segment2& s : base) {
            rotated.push_back({rotate(s.start, angle), rotate(s.end, angle)});
        }
        const DominantAxis rotatedAxis = findDominantAxis(rotated);
        if (!rotatedAxis.valid) continue;
        const Point2 expectedDirection = rotate(original.direction, angle);
        expect(std::abs(std::abs(dot(rotatedAxis.direction, expectedDirection)) - 1.0) < 1e-6,
            "rotating all segments rotates the dominant direction (up to sign)");
        expect(near(rotatedAxis.confidence, original.confidence, 1e-9),
            "rotation preserves dominant-axis confidence");
    }
}

// ---------------------------------------------------- segment intersection

void propertySegmentIntersectionOnSegments()
{
    std::mt19937_64 rng(0xC0FFEE7);
    for (int iteration = 0; iteration < 400; ++iteration) {
        const Segment2 query = randomSegments(rng, 1)[0];
        const Segment2 candidate = randomSegments(rng, 1)[0];
        Point2 point;
        double parameter = -1.0;
        if (!segmentIntersection(query, candidate, point, parameter)) continue;
        expect(parameter >= -1e-9 && parameter <= 1.0 + 1e-9, "query parameter is clamped into [0,1]");
        const double scale = std::max({length(query.end - query.start), length(candidate.end - candidate.start), 1.0});
        const double epsilon = std::max(1e-6, 1e-9 * scale);
        expect(distanceToSegment(point, query) <= epsilon, "intersection point lies on the query segment");
        expect(distanceToSegment(point, candidate) <= epsilon, "intersection point lies on the candidate segment");
    }
}

void propertySegmentIntersectionSymmetric()
{
    std::mt19937_64 rng(0xC0FFEE8);
    for (int iteration = 0; iteration < 400; ++iteration) {
        const Segment2 first = randomSegments(rng, 1)[0];
        const Segment2 second = randomSegments(rng, 1)[0];
        Point2 forward;
        double forwardParameter = -1.0;
        Point2 backward;
        double backwardParameter = -1.0;
        const bool hitForward = segmentIntersection(first, second, forward, forwardParameter);
        const bool hitBackward = segmentIntersection(second, first, backward, backwardParameter);
        if (hitForward && hitBackward) {
            const double scale = std::max({length(first.end - first.start), length(second.end - second.start), 1.0});
            const double epsilon = std::max(1e-6, 1e-9 * scale);
            expect(std::hypot(forward.x - backward.x, forward.y - backward.y) <= epsilon,
                "swapping segment order reports the same intersection point");
        }
    }
}

// --------------------------------------------------- ellipse / arc intersection

void propertyEllipseIntersectionsOnEllipse()
{
    std::mt19937_64 rng(0xC0FFEE9);
    for (int iteration = 0; iteration < 300; ++iteration) {
        const Segment2 query = randomSegments(rng, 1)[0];
        const Point2 center{std::uniform_real_distribution<double>(-50.0, 50.0)(rng),
            std::uniform_real_distribution<double>(-50.0, 50.0)(rng)};
        const double radiusX = std::uniform_real_distribution<double>(1.0, 40.0)(rng);
        const double radiusY = std::uniform_real_distribution<double>(1.0, 40.0)(rng);
        const double rotation = std::uniform_real_distribution<double>(-3.14, 3.14)(rng);
        const auto intersections = segmentEllipseIntersections(
            query, center, radiusX, radiusY, rotation, 0.0, 2.0 * kPi);
        expect(intersections.size() <= 2, "a line intersects a non-degenerate ellipse at most twice");
        const double scale = std::max({length(query.end - query.start), radiusX, radiusY});
        const double epsilon = std::max(1e-6, 1e-9 * scale);
        for (const auto& entry : intersections) {
            const double parameter = entry.first;
            const Point2& point = entry.second;
            expect(parameter >= -1e-9 && parameter <= 1.0 + 1e-9, "ellipse hit parameter is in [0,1]");
            const double dx = point.x - center.x;
            const double dy = point.y - center.y;
            const double u = std::cos(rotation) * dx + std::sin(rotation) * dy;
            const double v = -std::sin(rotation) * dx + std::cos(rotation) * dy;
            const double unit = (u / radiusX) * (u / radiusX) + (v / radiusY) * (v / radiusY);
            expect(std::abs(unit - 1.0) <= 1e-7, "intersection point satisfies the ellipse equation");
            expect(distanceToSegment(point, query) <= epsilon, "ellipse hit lies on the query segment");
        }
    }
}

// ------------------------------------------------------------ interval lanes

void propertyIntervalLanesNoOverlapInSameLane()
{
    std::mt19937_64 rng(0xC0FFEEA);
    const double gap = 1.0;
    for (int iteration = 0; iteration < 300; ++iteration) {
        std::vector<Interval> intervals;
        const std::size_t count = 2 + rng() % 40;
        intervals.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            const double start = std::uniform_real_distribution<double>(-100.0, 100.0)(rng);
            const double width = std::uniform_real_distribution<double>(0.1, 30.0)(rng);
            intervals.push_back({start, start + width, i});
        }
        const std::vector<std::size_t> lanes = assignIntervalLanes(intervals, gap);
        expect(lanes.size() == intervals.size(), "one lane per interval");
        std::vector<std::size_t> order(intervals.size());
        for (std::size_t i = 0; i < intervals.size(); ++i) order[i] = i;
        std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
            const Interval& ia = intervals[a];
            const Interval& ib = intervals[b];
            return ia.minimum < ib.minimum || (ia.minimum == ib.minimum && ia.maximum < ib.maximum);
        });
        for (std::size_t a = 0; a < order.size(); ++a) {
            for (std::size_t b = a + 1; b < order.size(); ++b) {
                const Interval& ia = intervals[order[a]];
                const Interval& ib = intervals[order[b]];
                if (lanes[ia.sourceIndex] != lanes[ib.sourceIndex]) continue;
                expect(ia.maximum + gap < ib.minimum - 1e-12,
                    "intervals sharing a lane are disjoint past the gap");
            }
        }
    }
}

// --------------------------------------------------------------- chains

void propertyChainsUseEverySegmentOnce()
{
    std::mt19937_64 rng(0xC0FFEEB);
    for (int iteration = 0; iteration < 200; ++iteration) {
        const std::vector<Segment2> segments = randomSegments(rng, 1 + rng() % 30);
        const double epsilon = TolerancePolicy{}.linear(geometryScale(segments));
        std::size_t nonDegenerate = 0;
        for (const Segment2& segment : segments) {
            if (length(segment.end - segment.start) > epsilon) ++nonDegenerate;
        }
        const auto chains = orderSegmentChains(segments);
        std::size_t total = 0;
        for (const auto& chain : chains) total += chain.size();
        expect(total == nonDegenerate, "every non-degenerate segment appears in exactly one chain");
    }
}

void propertyChainEdgesContiguous()
{
    std::mt19937_64 rng(0xC0FFEEC);
    for (int iteration = 0; iteration < 200; ++iteration) {
        const std::vector<Segment2> segments = randomSegments(rng, 2 + rng() % 30);
        const auto chains = orderSegmentChains(segments);
        for (const auto& chain : chains) {
            for (std::size_t i = 1; i < chain.size(); ++i) {
                expect(length(chain[i - 1].end - chain[i].start) < 1e-6,
                    "consecutive chain edges are endpoint-contiguous");
            }
        }
    }
}

// ------------------------------------------------------------- set cover

void propertySetCoverCoversAllFeatures()
{
    std::mt19937_64 rng(0xC0FFEED);
    for (int iteration = 0; iteration < 200; ++iteration) {
        const std::size_t featureCount = 1 + rng() % 20;
        std::vector<bool> anyCandidate(featureCount, false);
        std::vector<CoverageCandidate> candidates;
        const std::size_t candidateCount = 1 + rng() % 30;
        candidates.reserve(candidateCount);
        for (std::size_t i = 0; i < candidateCount; ++i) {
            CoverageCandidate candidate;
            const std::size_t size = 1 + rng() % 4;
            for (std::size_t k = 0; k < size; ++k) {
                const std::size_t feature = rng() % featureCount;
                candidate.features.push_back(feature);
                anyCandidate[feature] = true;
            }
            candidate.cost = std::uniform_real_distribution<double>(0.5, 5.0)(rng);
            candidate.sourceIndex = i;
            candidates.push_back(std::move(candidate));
        }
        // Guarantee every feature is coverable so the algorithm must cover it.
        for (std::size_t feature = 0; feature < featureCount; ++feature) {
            if (anyCandidate[feature]) continue;
            candidates.push_back({{feature}, 1.0, candidates.size()});
            anyCandidate[feature] = true;
        }
        const auto selected = selectCoveringCandidates(featureCount, candidates);
        std::set<std::size_t> seen;
        std::vector<bool> covered(featureCount, false);
        for (std::size_t index : selected) {
            expect(index < candidates.size(), "selected candidate index is in range");
            expect(seen.insert(index).second, "selected candidates are distinct");
            for (std::size_t feature : candidates[index].features) {
                if (feature < featureCount) covered[feature] = true;
            }
        }
        for (std::size_t feature = 0; feature < featureCount; ++feature) {
            expect(covered[feature], "every coverable feature is covered by the selection");
        }
    }
}

// ---------------------------------------------------------- label layout

void propertyLabelLayoutTranslationEquivariance()
{
    std::mt19937_64 rng(0xC0FFEEE);
    for (int iteration = 0; iteration < 100; ++iteration) {
        std::vector<LabelInput> labels;
        std::vector<LabelBox> obstacles;
        const std::size_t labelCount = 1 + rng() % 8;
        for (std::size_t i = 0; i < labelCount; ++i) {
            const double x = std::uniform_real_distribution<double>(-30.0, 30.0)(rng);
            const double y = std::uniform_real_distribution<double>(-30.0, 30.0)(rng);
            const double w = std::uniform_real_distribution<double>(1.0, 8.0)(rng);
            const double h = std::uniform_real_distribution<double>(1.0, 4.0)(rng);
            labels.push_back({
                {x, y, x + w, y + h},
                {std::uniform_real_distribution<double>(-1.0, 1.0)(rng),
                    std::uniform_real_distribution<double>(-1.0, 1.0)(rng)},
                1.0 + rng() % 5,
                1.0 + rng() % 5,
                1.0,
            });
        }
        const std::size_t obstacleCount = rng() % 3;
        for (std::size_t i = 0; i < obstacleCount; ++i) {
            const double x = std::uniform_real_distribution<double>(-30.0, 30.0)(rng);
            const double y = std::uniform_real_distribution<double>(-30.0, 30.0)(rng);
            obstacles.push_back({x, y, x + 10.0, y + 10.0});
        }
        const Point2 delta{std::uniform_real_distribution<double>(-50.0, 50.0)(rng),
            std::uniform_real_distribution<double>(-50.0, 50.0)(rng)};

        const auto basePlacements = optimizeLabelLayout(labels, obstacles, 3, 4, 0.1);
        std::vector<LabelInput> shiftedLabels;
        shiftedLabels.reserve(labels.size());
        for (const LabelInput& label : labels) {
            shiftedLabels.push_back({
                {label.box.minX + delta.x, label.box.minY + delta.y, label.box.maxX + delta.x, label.box.maxY + delta.y},
                label.axis,
                label.alongStep,
                label.normalStep,
                label.movementWeight,
            });
        }
        std::vector<LabelBox> shiftedObstacles;
        for (const LabelBox& obstacle : obstacles) shiftedObstacles.push_back(translated(obstacle, delta));
        const auto shiftedPlacements = optimizeLabelLayout(shiftedLabels, shiftedObstacles, 3, 4, 0.1);

        expect(basePlacements.size() == labels.size(), "layout returns every label");
        expect(shiftedPlacements.size() == shiftedLabels.size(), "shifted layout returns every label");
        for (std::size_t i = 0; i < labels.size(); ++i) {
            expect(basePlacements[i].alongSteps == shiftedPlacements[i].alongSteps &&
                    basePlacements[i].normalSteps == shiftedPlacements[i].normalSteps,
                "optimal label movement is translation-invariant");
            const Point2 difference{
                (shiftedPlacements[i].box.minX - delta.x) - basePlacements[i].box.minX,
                (shiftedPlacements[i].box.minY - delta.y) - basePlacements[i].box.minY,
            };
            expect(std::hypot(difference.x, difference.y) < 1e-9,
                "translated layout is the base layout translated by the same delta");
        }
    }
}

void propertyLabelMovementIsBounded()
{
    std::mt19937_64 rng(0xC0FFEEF);
    for (int iteration = 0; iteration < 100; ++iteration) {
        std::vector<LabelInput> labels;
        const std::size_t labelCount = 1 + rng() % 6;
        for (std::size_t i = 0; i < labelCount; ++i) {
            const double x = std::uniform_real_distribution<double>(-20.0, 20.0)(rng);
            const double y = std::uniform_real_distribution<double>(-20.0, 20.0)(rng);
            labels.push_back({{x, y, x + 4.0, y + 2.0}, {1.0, 0.0}, 1.0, 1.0, 1.0});
        }
        const auto placements = optimizeLabelLayout(labels, {}, 3, 4, 0.1);
        for (const LabelPlacement& placement : placements) {
            expect(placement.alongSteps >= -3 && placement.alongSteps <= 3 &&
                    placement.normalSteps >= -4 && placement.normalSteps <= 4,
                "label stays within the bounded candidate grid");
        }
    }
}

// ------------------------------------------------- tolerance policy props

void propertyToleranceMonotone()
{
    std::mt19937_64 rng(0xC0FFFF0);
    const TolerancePolicy tolerance{1e-6, 1e-9, 1e-8};
    for (int iteration = 0; iteration < 100; ++iteration) {
        const double a = std::uniform_real_distribution<double>(0.0, 1e9)(rng);
        const double b = std::uniform_real_distribution<double>(0.0, 1e9)(rng);
        const double lo = std::min(a, b);
        const double hi = std::max(a, b);
        expect(tolerance.linear(lo) <= tolerance.linear(hi) + 1e-18,
            "linear tolerance is non-decreasing with geometry scale");
        expect(tolerance.linear(0.0) >= tolerance.absolute, "linear tolerance never drops below the absolute floor");
    }
}

// ------------------------------------------------- robustness / robustness

void propertyNoCrashOnDegenerateInputs()
{
    const std::vector<Point2> empty;
    const std::vector<Segment2> noSegments;
    expect(convexHull(empty).empty(), "empty hull input returns empty");
    expect(minimumAreaBounds(empty).valid == false, "empty bounds input is invalid");
    expect(orderSegmentChains(noSegments).empty(), "empty chain input returns empty");
    expect(assignIntervalLanes({}, 1.0).empty(), "empty lane input returns empty");
    expect(selectCoveringCandidates(0, {}).empty(), "empty cover input returns empty");

    std::mt19937_64 rng(0xC0FFFF1);
    for (int iteration = 0; iteration < 200; ++iteration) {
        const double huge = 1e12;
        const double tiny = 1e-12;
        std::vector<Point2> points;
        const std::size_t count = 2 + rng() % 30;
        for (std::size_t i = 0; i < count; ++i) {
            const double mode = rng() % 3;
            const double value = mode == 0 ? std::uniform_real_distribution<double>(-huge, huge)(rng)
                : mode == 1 ? std::uniform_real_distribution<double>(-tiny, tiny)(rng)
                            : std::uniform_real_distribution<double>(-1.0, 1.0)(rng);
            const double x = value;
            const double y = rng() % 2 == 0 ? value : std::uniform_real_distribution<double>(-huge, huge)(rng);
            points.push_back({x, y});
        }
        const auto hull = convexHull(points);
        for (const Point2& p : hull) expect(finite(p), "hull output is finite");
        const OrientedBounds bounds = minimumAreaBounds(points);
        if (bounds.valid) {
            expect(finite(bounds.widthStart) && finite(bounds.widthEnd) &&
                    finite(bounds.heightStart) && finite(bounds.heightEnd),
                "oriented bounds corners are finite");
            expect(std::isfinite(bounds.area) && std::isfinite(bounds.width) && std::isfinite(bounds.height),
                "oriented bounds metrics are finite");
        }
        std::vector<Segment2> segments;
        for (const Point2& p : points) {
            segments.push_back({p, p});
        }
        const auto chains = orderSegmentChains(segments);
        for (const auto& chain : chains) {
            for (const Segment2& s : chain) expect(finite(s.start) && finite(s.end), "chain segments are finite");
        }
    }
}

void propertyNoNanInIntersectionOutputs()
{
    std::mt19937_64 rng(0xC0FFFF2);
    for (int iteration = 0; iteration < 200; ++iteration) {
        const Segment2 query = randomSegments(rng, 1)[0];
        const Segment2 candidate = randomSegments(rng, 1)[0];
        Point2 point;
        double parameter = -1.0;
        if (segmentIntersection(query, candidate, point, parameter)) {
            expect(finite(point), "segment intersection point is finite");
            expect(std::isfinite(parameter), "segment intersection parameter is finite");
        }
        const auto ellipse = segmentEllipseIntersections(
            query, {0.0, 0.0}, 10.0, 5.0, 0.0, 0.0, 2.0 * kPi);
        for (const auto& entry : ellipse) {
            expect(finite(entry.second), "ellipse intersection point is finite");
            expect(std::isfinite(entry.first), "ellipse intersection parameter is finite");
        }
    }
}

void runAll()
{
    propertyConvexHullContainsAllPoints();
    propertyConvexHullIsConvex();
    propertyMinAreaBoundsContainsAllPoints();
    propertyMinAreaBoundsEquivariance();
    propertyDominantAxisInvariants();
    propertyDominantAxisRotationEquivariance();
    propertySegmentIntersectionOnSegments();
    propertySegmentIntersectionSymmetric();
    propertyEllipseIntersectionsOnEllipse();
    propertyIntervalLanesNoOverlapInSameLane();
    propertyChainsUseEverySegmentOnce();
    propertyChainEdgesContiguous();
    propertySetCoverCoversAllFeatures();
    propertyLabelLayoutTranslationEquivariance();
    propertyLabelMovementIsBounded();
    propertyToleranceMonotone();
    propertyNoCrashOnDegenerateInputs();
    propertyNoNanInIntersectionOutputs();
}

} // namespace

int main()
{
    runAll();
    if (failures != 0) {
        std::cerr << failures << " property test(s) failed\n";
        return 1;
    }
    std::cout << "All AutoDimensionAlgorithms property tests passed\n";
    return 0;
}
