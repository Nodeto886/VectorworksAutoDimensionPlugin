#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <queue>
#include <unordered_map>
#include <utility>
#include <vector>

namespace vwad::algo {

constexpr double kPi = 3.141592653589793238462643383279502884;

struct Point2 {
    double x{0.0};
    double y{0.0};
};

inline Point2 operator+(const Point2& a, const Point2& b) { return {a.x + b.x, a.y + b.y}; }
inline Point2 operator-(const Point2& a, const Point2& b) { return {a.x - b.x, a.y - b.y}; }
inline Point2 operator*(const Point2& point, double scalar) { return {point.x * scalar, point.y * scalar}; }
inline double dot(const Point2& a, const Point2& b) { return a.x * b.x + a.y * b.y; }
inline double cross(const Point2& a, const Point2& b) { return a.x * b.y - a.y * b.x; }
inline double lengthSquared(const Point2& value) { return dot(value, value); }
inline double length(const Point2& value) { return std::hypot(value.x, value.y); }

inline Point2 normalized(const Point2& value, double epsilon = 1e-12)
{
    const double magnitude = length(value);
    return magnitude > epsilon ? value * (1.0 / magnitude) : Point2{};
}

struct Segment2 {
    Point2 start;
    Point2 end;
};

struct Bounds2 {
    double minX{std::numeric_limits<double>::max()};
    double minY{std::numeric_limits<double>::max()};
    double maxX{std::numeric_limits<double>::lowest()};
    double maxY{std::numeric_limits<double>::lowest()};

    void add(const Point2& point)
    {
        minX = std::min(minX, point.x);
        minY = std::min(minY, point.y);
        maxX = std::max(maxX, point.x);
        maxY = std::max(maxY, point.y);
    }

    [[nodiscard]] bool valid() const { return minX <= maxX && minY <= maxY; }
    [[nodiscard]] double width() const { return valid() ? maxX - minX : 0.0; }
    [[nodiscard]] double height() const { return valid() ? maxY - minY : 0.0; }
    [[nodiscard]] double diagonal() const { return std::hypot(width(), height()); }
};

struct TolerancePolicy {
    double absolute{1e-6};
    double relative{1e-9};
    double angular{1e-8};

    [[nodiscard]] double linear(double scale) const
    {
        return std::max(absolute, relative * std::max(1.0, std::abs(scale)));
    }
};

// Returns the signed offset that places a candidate dimension on a reference
// world-space dimension line. Opposite endpoint order is handled by the sign of
// the candidate normal.
inline bool alignedDimensionOffset(
    const Segment2& reference,
    double referenceOffset,
    const Segment2& candidate,
    double& outOffset,
    const TolerancePolicy& tolerance = {})
{
    const Point2 referenceDelta = reference.end - reference.start;
    const Point2 candidateDelta = candidate.end - candidate.start;
    const double referenceLength = length(referenceDelta);
    const double candidateLength = length(candidateDelta);
    const double epsilon = tolerance.linear(std::max(referenceLength, candidateLength));
    if (referenceLength <= epsilon || candidateLength <= epsilon) return false;

    const Point2 referenceDirection = referenceDelta * (1.0 / referenceLength);
    const Point2 candidateDirection = candidateDelta * (1.0 / candidateLength);
    if (std::abs(cross(referenceDirection, candidateDirection)) > std::max(tolerance.angular, 1e-6)) return false;

    const Point2 referenceNormal{-referenceDirection.y, referenceDirection.x};
    const Point2 candidateNormal{-candidateDirection.y, candidateDirection.x};
    const Point2 referenceLinePoint = reference.start + referenceNormal * referenceOffset;
    outOffset = dot(referenceLinePoint - candidate.start, candidateNormal);
    return std::isfinite(outOffset);
}

// True when the (roughly unit-length) axis points along the vertical (Y) direction.
// Vectorworks measures a constrained (ortho) vertical dimension with a positive
// start offset to the RIGHT, but the unified candidate normal (-directionY,
// directionX) points LEFT for a vertical axis. Callers that rebuild ortho vertical
// dimensions must flip the offset sign so the dimension lands on the same side as
// the clicked reference line instead of being mirrored across the witness origin.
inline bool isAxisVertical(double axisX, double axisY, double epsilon = 1e-9)
{
    return std::abs(axisX) <= epsilon && std::abs(axisY) > epsilon;
}

// Vectorworks measures a constrained (ortho) vertical dimension with a positive start
// offset to the RIGHT, while the unified candidate normal (-directionY, directionX)
// points LEFT for a vertical axis. When aligning an ortho (class 0) vertical dimension,
// flip the derived target offset so the rebuilt dimension lands on the clicked side
// instead of being mirrored across the witness origin. Horizontal ortho and aligned
// dimensions keep the derived offset as-is.
inline double orthoVerticalAlignOffset(double offset, unsigned char dimensionClass, double axisX, double axisY)
{
    return (dimensionClass == 0 && isAxisVertical(axisX, axisY)) ? -offset : offset;
}

// Reversing the endpoints of an ALIGNED dimension flips its normal (-dy,dx)/L, so the
// offset must be negated to keep the line on the same side. Ortho (corner) dimensions use
// an axis-fixed normal (up for horizontal, right for vertical) that does not depend on
// endpoint order, so their offset is left unchanged; negating it would mirror the line to
// the opposite side.
inline double reversedDimensionOffset(double offset, unsigned char dimensionClass)
{
    return (dimensionClass == 1) ? -offset : offset;
}

inline double geometryScale(const std::vector<Point2>& points)
{
    Bounds2 bounds;
    for (const Point2& point : points) bounds.add(point);
    return bounds.diagonal();
}

inline double geometryScale(const std::vector<Segment2>& segments)
{
    Bounds2 bounds;
    for (const Segment2& segment : segments) {
        bounds.add(segment.start);
        bounds.add(segment.end);
    }
    return bounds.diagonal();
}

struct DominantAxis {
    Point2 direction{1.0, 0.0};
    Segment2 reference;
    double confidence{0.0};
    double supportingLength{0.0};
    double foldedAngleDegrees{0.0};
    bool valid{false};
};

// Four-angle circular statistics treats perpendicular edges as evidence for the
// same local coordinate frame. This avoids the discontinuities of fixed bins.
inline DominantAxis findDominantAxis(
    const std::vector<Segment2>& segments,
    const TolerancePolicy& tolerance = {})
{
    DominantAxis result;
    const double epsilon = tolerance.linear(geometryScale(segments));
    double sumCos4 = 0.0;
    double sumSin4 = 0.0;
    double totalWeight = 0.0;
    for (const Segment2& segment : segments) {
        const Point2 delta = segment.end - segment.start;
        const double weight = length(delta);
        if (weight <= epsilon) continue;
        const double theta = std::atan2(delta.y, delta.x);
        sumCos4 += weight * std::cos(4.0 * theta);
        sumSin4 += weight * std::sin(4.0 * theta);
        totalWeight += weight;
    }
    if (totalWeight <= epsilon) return result;

    double baseAngle = 0.25 * std::atan2(sumSin4, sumCos4);
    while (baseAngle < 0.0) baseAngle += kPi * 0.5;
    while (baseAngle >= kPi * 0.5) baseAngle -= kPi * 0.5;
    const Point2 candidates[2] = {
        {std::cos(baseAngle), std::sin(baseAngle)},
        {-std::sin(baseAngle), std::cos(baseAngle)},
    };

    double supports[2] = {0.0, 0.0};
    const Segment2* references[2] = {nullptr, nullptr};
    double referenceLengths[2] = {0.0, 0.0};
    constexpr double kAlignmentPower = 8.0;
    for (const Segment2& segment : segments) {
        const Point2 delta = segment.end - segment.start;
        const double segmentLength = length(delta);
        if (segmentLength <= epsilon) continue;
        const Point2 unit = delta * (1.0 / segmentLength);
        for (std::size_t axis = 0; axis < 2; ++axis) {
            const double alignment = std::abs(dot(unit, candidates[axis]));
            const double evidence = segmentLength * std::pow(alignment, kAlignmentPower);
            supports[axis] += evidence;
            if (alignment >= std::cos(15.0 * kPi / 180.0) && segmentLength > referenceLengths[axis]) {
                references[axis] = &segment;
                referenceLengths[axis] = segmentLength;
            }
        }
    }

    const std::size_t selected = supports[1] > supports[0] ? 1 : 0;
    result.direction = candidates[selected];
    if (result.direction.x < -epsilon ||
        (std::abs(result.direction.x) <= epsilon && result.direction.y < 0.0)) {
        result.direction = result.direction * -1.0;
    }
    if (references[selected]) result.reference = *references[selected];
    else {
        result.reference.start = {0.0, 0.0};
        result.reference.end = result.direction;
    }
    result.supportingLength = supports[selected];
    result.confidence = std::hypot(sumCos4, sumSin4) / totalWeight;
    double angle = std::atan2(std::abs(result.direction.y), std::abs(result.direction.x));
    angle = std::min(angle, std::abs(kPi * 0.5 - angle));
    result.foldedAngleDegrees = angle * 180.0 / kPi;
    result.valid = true;
    return result;
}

inline std::vector<Point2> convexHull(
    std::vector<Point2> points,
    const TolerancePolicy& tolerance = {})
{
    if (points.size() <= 1) return points;
    const double epsilon = tolerance.linear(geometryScale(points));
    std::sort(points.begin(), points.end(), [](const Point2& a, const Point2& b) {
        return a.x < b.x || (a.x == b.x && a.y < b.y);
    });
    points.erase(std::unique(points.begin(), points.end(), [epsilon](const Point2& a, const Point2& b) {
        return lengthSquared(a - b) <= epsilon * epsilon;
    }), points.end());
    if (points.size() <= 2) return points;

    std::vector<Point2> hull;
    hull.reserve(points.size() * 2);
    const auto append = [&](const Point2& point, std::vector<Point2>& output) {
        while (output.size() >= 2) {
            const Point2 a = output[output.size() - 2];
            const Point2 b = output.back();
            const Point2 first = b - a;
            const Point2 second = point - b;
            const double orientationTolerance = std::max(
                epsilon * epsilon,
                tolerance.angular * length(first) * length(second));
            if (cross(first, second) > orientationTolerance) break;
            output.pop_back();
        }
        output.push_back(point);
    };
    for (const Point2& point : points) append(point, hull);
    const std::size_t lowerSize = hull.size();
    for (std::size_t index = points.size() - 1; index-- > 0;) append(points[index], hull);
    if (hull.size() > lowerSize) hull.pop_back();
    return hull;
}

struct OrientedBounds {
    Point2 axis{1.0, 0.0};
    Point2 normal{0.0, 1.0};
    Point2 widthStart;
    Point2 widthEnd;
    Point2 heightStart;
    Point2 heightEnd;
    double width{0.0};
    double height{0.0};
    double area{0.0};
    bool valid{false};
};

inline OrientedBounds minimumAreaBounds(
    const std::vector<Point2>& points,
    const TolerancePolicy& tolerance = {})
{
    OrientedBounds best;
    const std::vector<Point2> hull = convexHull(points, tolerance);
    if (hull.size() < 2) return best;
    const double epsilon = tolerance.linear(geometryScale(hull));
    double bestArea = std::numeric_limits<double>::max();
    double bestPerimeter = std::numeric_limits<double>::max();

    for (std::size_t edge = 0; edge < hull.size(); ++edge) {
        Point2 axis = normalized(hull[(edge + 1) % hull.size()] - hull[edge], epsilon);
        if (lengthSquared(axis) == 0.0) continue;
        if (axis.x < -epsilon || (std::abs(axis.x) <= epsilon && axis.y < 0.0)) axis = axis * -1.0;
        const Point2 normal{-axis.y, axis.x};
        double minU = std::numeric_limits<double>::max();
        double maxU = std::numeric_limits<double>::lowest();
        double minV = std::numeric_limits<double>::max();
        double maxV = std::numeric_limits<double>::lowest();
        for (const Point2& point : hull) {
            const double u = dot(point, axis);
            const double v = dot(point, normal);
            minU = std::min(minU, u);
            maxU = std::max(maxU, u);
            minV = std::min(minV, v);
            maxV = std::max(maxV, v);
        }
        const double width = maxU - minU;
        const double height = maxV - minV;
        const double area = width * height;
        const double perimeter = 2.0 * (width + height);
        if (area > bestArea + epsilon ||
            (std::abs(area - bestArea) <= epsilon && perimeter >= bestPerimeter)) continue;

        bestArea = area;
        bestPerimeter = perimeter;
        best.axis = axis;
        best.normal = normal;
        best.width = width;
        best.height = height;
        best.area = area;
        best.widthStart = axis * minU + normal * minV;
        best.widthEnd = axis * maxU + normal * minV;
        best.heightStart = best.widthEnd;
        best.heightEnd = axis * maxU + normal * maxV;
        best.valid = width > epsilon || height > epsilon;
    }
    return best;
}

struct CellKey {
    std::int64_t x{0};
    std::int64_t y{0};
    bool operator==(const CellKey& other) const { return x == other.x && y == other.y; }
};

struct CellKeyHash {
    std::size_t operator()(const CellKey& key) const
    {
        const std::uint64_t x = static_cast<std::uint64_t>(key.x);
        const std::uint64_t y = static_cast<std::uint64_t>(key.y);
        return static_cast<std::size_t>((x * 0x9E3779B185EBCA87ULL) ^ (y + 0xC2B2AE3D27D4EB4FULL));
    }
};

class SpatialPointIndex {
public:
    explicit SpatialPointIndex(double epsilon)
        : fEpsilon(std::max(epsilon, 1e-12))
    {
    }

    std::size_t insertOrFind(const Point2& point)
    {
        const std::size_t existing = find(point);
        if (existing != std::numeric_limits<std::size_t>::max()) return existing;
        const CellKey cell = key(point);
        const std::size_t index = fPoints.size();
        fPoints.push_back(point);
        fCells[cell].push_back(index);
        return index;
    }

    [[nodiscard]] std::size_t find(const Point2& point) const
    {
        const CellKey cell = key(point);
        for (std::int64_t dx = -1; dx <= 1; ++dx) {
            for (std::int64_t dy = -1; dy <= 1; ++dy) {
                const auto offset = [](std::int64_t value, std::int64_t delta) {
                    if (delta < 0 && value == std::numeric_limits<std::int64_t>::min()) return value;
                    if (delta > 0 && value == std::numeric_limits<std::int64_t>::max()) return value;
                    return value + delta;
                };
                const auto found = fCells.find({offset(cell.x, dx), offset(cell.y, dy)});
                if (found == fCells.end()) continue;
                for (std::size_t index : found->second) {
                    if (lengthSquared(fPoints[index] - point) <= fEpsilon * fEpsilon) return index;
                }
            }
        }
        return std::numeric_limits<std::size_t>::max();
    }

    [[nodiscard]] const std::vector<Point2>& points() const { return fPoints; }

private:
    std::int64_t cellCoordinate(double value) const
    {
        if (!std::isfinite(value)) return 0;
        const long double scaled = std::floor(static_cast<long double>(value) / fEpsilon);
        const long double minimum = static_cast<long double>(std::numeric_limits<std::int64_t>::min());
        const long double maximum = static_cast<long double>(std::numeric_limits<std::int64_t>::max());
        if (scaled <= minimum) return std::numeric_limits<std::int64_t>::min();
        if (scaled >= maximum) return std::numeric_limits<std::int64_t>::max();
        return static_cast<std::int64_t>(scaled);
    }

    CellKey key(const Point2& point) const
    {
        return {cellCoordinate(point.x), cellCoordinate(point.y)};
    }

    double fEpsilon;
    std::vector<Point2> fPoints;
    std::unordered_map<CellKey, std::vector<std::size_t>, CellKeyHash> fCells;
};

inline std::vector<std::vector<Segment2>> orderSegmentChains(
    const std::vector<Segment2>& segments,
    const TolerancePolicy& tolerance = {})
{
    std::vector<std::vector<Segment2>> chains;
    if (segments.empty()) return chains;
    const double epsilon = tolerance.linear(geometryScale(segments));
    SpatialPointIndex pointIndex(epsilon);
    struct Edge { std::size_t a; std::size_t b; bool used{false}; };
    std::vector<Edge> edges;
    edges.reserve(segments.size());
    for (const Segment2& segment : segments) {
        if (length(segment.end - segment.start) <= epsilon) continue;
        const std::size_t a = pointIndex.insertOrFind(segment.start);
        const std::size_t b = pointIndex.insertOrFind(segment.end);
        if (a != b) edges.push_back({a, b, false});
    }
    std::vector<std::vector<std::size_t>> adjacency(pointIndex.points().size());
    for (std::size_t edge = 0; edge < edges.size(); ++edge) {
        adjacency[edges[edge].a].push_back(edge);
        adjacency[edges[edge].b].push_back(edge);
    }

    const auto unusedDegree = [&](std::size_t vertex) {
        std::size_t degree = 0;
        for (std::size_t edge : adjacency[vertex]) if (!edges[edge].used) ++degree;
        return degree;
    };
    std::size_t remaining = edges.size();
    while (remaining > 0) {
        std::size_t startVertex = pointIndex.points().size();
        for (std::size_t vertex = 0; vertex < adjacency.size(); ++vertex) {
            if (unusedDegree(vertex) == 1) { startVertex = vertex; break; }
        }
        if (startVertex == pointIndex.points().size()) {
            for (std::size_t vertex = 0; vertex < adjacency.size(); ++vertex) {
                if (unusedDegree(vertex) > 0) { startVertex = vertex; break; }
            }
        }
        if (startVertex == pointIndex.points().size()) break;

        std::vector<Segment2> chain;
        std::size_t current = startVertex;
        Point2 previousDirection{};
        while (true) {
            std::size_t selectedEdge = edges.size();
            double selectedTurn = std::numeric_limits<double>::max();
            for (std::size_t edgeIndex : adjacency[current]) {
                if (edges[edgeIndex].used) continue;
                const Edge& edge = edges[edgeIndex];
                const std::size_t other = edge.a == current ? edge.b : edge.a;
                const Point2 direction = normalized(pointIndex.points()[other] - pointIndex.points()[current], epsilon);
                const double turn = lengthSquared(previousDirection) == 0.0
                    ? 0.0
                    : std::acos(std::clamp(dot(previousDirection, direction), -1.0, 1.0));
                if (turn < selectedTurn) {
                    selectedTurn = turn;
                    selectedEdge = edgeIndex;
                }
            }
            if (selectedEdge == edges.size()) break;
            Edge& edge = edges[selectedEdge];
            const std::size_t next = edge.a == current ? edge.b : edge.a;
            const Point2 start = pointIndex.points()[current];
            const Point2 end = pointIndex.points()[next];
            chain.push_back({start, end});
            previousDirection = normalized(end - start, epsilon);
            current = next;
            edge.used = true;
            --remaining;
        }
        if (!chain.empty()) chains.push_back(std::move(chain));
    }
    return chains;
}

inline bool segmentIntersection(
    const Segment2& query,
    const Segment2& candidate,
    Point2& outPoint,
    double& outQueryParameter,
    const TolerancePolicy& tolerance = {})
{
    const Point2 r = query.end - query.start;
    const Point2 s = candidate.end - candidate.start;
    const double rLength = length(r);
    const double sLength = length(s);
    const double scale = std::max(rLength, sLength);
    const double epsilon = tolerance.linear(scale);
    if (rLength <= epsilon || sLength <= epsilon) return false;
    const double determinant = cross(r, s);
    if (std::abs(determinant) <= tolerance.angular * rLength * sLength) return false;
    const Point2 qMinusP = candidate.start - query.start;
    const double t = cross(qMinusP, s) / determinant;
    const double u = cross(qMinusP, r) / determinant;
    const double parameterTolerance = epsilon / std::max(rLength, sLength);
    if (t < -parameterTolerance || t > 1.0 + parameterTolerance ||
        u < -parameterTolerance || u > 1.0 + parameterTolerance) return false;
    outQueryParameter = std::clamp(t, 0.0, 1.0);
    outPoint = query.start + r * outQueryParameter;
    return true;
}

inline double normalizeAngle(double angle)
{
    while (angle < 0.0) angle += 2.0 * kPi;
    while (angle >= 2.0 * kPi) angle -= 2.0 * kPi;
    return angle;
}

inline bool angleInSweep(double angle, double start, double sweep, double epsilon = 1e-9)
{
    if (std::abs(sweep) >= 2.0 * kPi - epsilon) return true;
    angle = normalizeAngle(angle);
    start = normalizeAngle(start);
    if (sweep >= 0.0) return normalizeAngle(angle - start) <= sweep + epsilon;
    return normalizeAngle(start - angle) <= -sweep + epsilon;
}

// rotation is the ellipse local X axis angle. startAngle/sweepAngle are in that
// local ellipse parameter space. A full ellipse uses a sweep of 2*pi.
inline std::vector<std::pair<double, Point2>> segmentEllipseIntersections(
    const Segment2& query,
    const Point2& center,
    double radiusX,
    double radiusY,
    double rotation,
    double startAngle,
    double sweepAngle,
    const TolerancePolicy& tolerance = {})
{
    std::vector<std::pair<double, Point2>> result;
    radiusX = std::abs(radiusX);
    radiusY = std::abs(radiusY);
    const double scale = std::max({length(query.end - query.start), radiusX, radiusY});
    const double epsilon = tolerance.linear(scale);
    if (radiusX <= epsilon || radiusY <= epsilon) return result;

    const double c = std::cos(rotation);
    const double s = std::sin(rotation);
    const auto toLocal = [&](const Point2& point) {
        const Point2 delta = point - center;
        return Point2{c * delta.x + s * delta.y, -s * delta.x + c * delta.y};
    };
    const Point2 p = toLocal(query.start);
    const Point2 q = toLocal(query.end);
    const Point2 d = q - p;
    const double invRx2 = 1.0 / (radiusX * radiusX);
    const double invRy2 = 1.0 / (radiusY * radiusY);
    const double a = d.x * d.x * invRx2 + d.y * d.y * invRy2;
    const double b = 2.0 * (p.x * d.x * invRx2 + p.y * d.y * invRy2);
    const double cc = p.x * p.x * invRx2 + p.y * p.y * invRy2 - 1.0;
    if (a <= epsilon * epsilon) return result;
    double discriminant = b * b - 4.0 * a * cc;
    // These coefficients are dimensionless. Using a world-coordinate epsilon
    // here grows the tolerance with drawing scale and can turn a miss into a
    // false tangent on large models.
    const double discriminantMagnitude = std::max(1.0, b * b + std::abs(4.0 * a * cc));
    const double discriminantTolerance = std::numeric_limits<double>::epsilon() * 128.0 * discriminantMagnitude;
    if (discriminant < -discriminantTolerance) return result;
    discriminant = std::max(0.0, discriminant);
    const double root = std::sqrt(discriminant);
    const double roots[2] = {(-b - root) / (2.0 * a), (-b + root) / (2.0 * a)};
    const std::size_t rootCount = root <= epsilon ? 1 : 2;
    const double parameterTolerance = epsilon / std::max(1.0, length(query.end - query.start));
    for (std::size_t index = 0; index < rootCount; ++index) {
        const double t = roots[index];
        if (t < -parameterTolerance || t > 1.0 + parameterTolerance) continue;
        const Point2 local = p + d * t;
        const double ellipseAngle = std::atan2(local.y / radiusY, local.x / radiusX);
        if (!angleInSweep(ellipseAngle, startAngle, sweepAngle, tolerance.angular)) continue;
        const double clamped = std::clamp(t, 0.0, 1.0);
        result.push_back({clamped, query.start + (query.end - query.start) * clamped});
    }
    return result;
}

struct Interval {
    double minimum{0.0};
    double maximum{0.0};
    std::size_t sourceIndex{0};
};

inline std::vector<std::size_t> assignIntervalLanes(std::vector<Interval> intervals, double gap)
{
    std::vector<std::size_t> lanes(intervals.size(), 0);
    std::sort(intervals.begin(), intervals.end(), [](const Interval& a, const Interval& b) {
        return a.minimum < b.minimum || (a.minimum == b.minimum && a.maximum < b.maximum);
    });
    using LaneEnd = std::pair<double, std::size_t>;
    std::priority_queue<LaneEnd, std::vector<LaneEnd>, std::greater<LaneEnd>> active;
    std::priority_queue<std::size_t, std::vector<std::size_t>, std::greater<std::size_t>> available;
    std::size_t laneCount = 0;
    for (const Interval& interval : intervals) {
        while (!active.empty() && active.top().first + gap < interval.minimum) {
            available.push(active.top().second);
            active.pop();
        }
        const std::size_t lane = available.empty() ? laneCount++ : available.top();
        if (!available.empty()) available.pop();
        lanes[interval.sourceIndex] = lane;
        active.push({interval.maximum, lane});
    }
    return lanes;
}

inline std::vector<std::pair<std::size_t, std::size_t>> orthogonalSpanningTree(
    const std::vector<Point2>& points,
    const TolerancePolicy& tolerance = {})
{
    std::vector<std::pair<std::size_t, std::size_t>> edges;
    if (points.size() < 2) return edges;
    const double epsilon = tolerance.linear(geometryScale(points));

    Point2 centroid{};
    for (const Point2& point : points) centroid = centroid + point;
    centroid = centroid * (1.0 / static_cast<double>(points.size()));
    double xx = 0.0;
    double xy = 0.0;
    double yy = 0.0;
    for (const Point2& point : points) {
        const Point2 delta = point - centroid;
        xx += delta.x * delta.x;
        xy += delta.x * delta.y;
        yy += delta.y * delta.y;
    }
    double axisAngle = 0.0;
    const double covarianceMagnitude = std::max({std::abs(xx), std::abs(xy), std::abs(yy), epsilon * epsilon});
    const bool isotropic = std::hypot(xx - yy, 2.0 * xy) <= covarianceMagnitude * 1e-9;
    if (!isotropic) {
        axisAngle = 0.5 * std::atan2(2.0 * xy, xx - yy);
    }
    else {
        // PCA has no preferred axis for symmetric squares/grids. Recover a
        // stable local axis from the shortest point spacing rather than world X.
        double shortestDistance = std::numeric_limits<double>::max();
        Point2 shortestDirection{1.0, 0.0};
        for (std::size_t first = 0; first < points.size(); ++first) {
            for (std::size_t second = first + 1; second < points.size(); ++second) {
                Point2 delta = points[second] - points[first];
                const double distance = length(delta);
                if (distance <= epsilon || distance > shortestDistance + epsilon) continue;
                delta = delta * (1.0 / distance);
                if (delta.x < 0.0 || (std::abs(delta.x) <= tolerance.angular && delta.y < 0.0)) delta = delta * -1.0;
                shortestDistance = distance;
                shortestDirection = delta;
            }
        }
        axisAngle = std::atan2(shortestDirection.y, shortestDirection.x);
    }
    const Point2 axis{std::cos(axisAngle), std::sin(axisAngle)};
    const Point2 normal{-axis.y, axis.x};

    const std::size_t count = points.size();
    const std::size_t none = std::numeric_limits<std::size_t>::max();
    std::vector<bool> connected(count, false);
    std::vector<double> bestCost(count, std::numeric_limits<double>::max());
    std::vector<std::size_t> parent(count, none);
    bestCost[0] = 0.0;
    for (std::size_t iteration = 0; iteration < count; ++iteration) {
        std::size_t next = none;
        for (std::size_t index = 0; index < count; ++index) {
            if (!connected[index] && (next == none || bestCost[index] < bestCost[next])) next = index;
        }
        if (next == none) break;
        connected[next] = true;
        if (parent[next] != none) edges.push_back({parent[next], next});
        for (std::size_t candidate = 0; candidate < count; ++candidate) {
            if (connected[candidate]) continue;
            const Point2 delta = points[candidate] - points[next];
            const double distance = length(delta);
            if (distance <= epsilon) continue;
            const Point2 direction = delta * (1.0 / distance);
            const double offAxis = std::min(std::abs(dot(direction, axis)), std::abs(dot(direction, normal)));
            const double alignmentPenalty = 1.0 + 2.0 * offAxis;
            const double cost = distance * alignmentPenalty;
            if (cost < bestCost[candidate]) {
                bestCost[candidate] = cost;
                parent[candidate] = next;
            }
        }
    }
    return edges;
}

struct LabelBox {
    double minX{0.0};
    double minY{0.0};
    double maxX{0.0};
    double maxY{0.0};
};

inline LabelBox translated(const LabelBox& box, const Point2& delta)
{
    return {box.minX + delta.x, box.minY + delta.y, box.maxX + delta.x, box.maxY + delta.y};
}

inline double overlapArea(const LabelBox& a, const LabelBox& b, double padding = 0.0)
{
    const double width = std::min(a.maxX, b.maxX) - std::max(a.minX, b.minX) + padding;
    const double height = std::min(a.maxY, b.maxY) - std::max(a.minY, b.minY) + padding;
    return width > 0.0 && height > 0.0 ? width * height : 0.0;
}

struct LabelInput {
    LabelBox box;
    Point2 axis{1.0, 0.0};
    double alongStep{1.0};
    double normalStep{1.0};
    double movementWeight{1.0};
};

struct LabelPlacement {
    int alongSteps{0};
    int normalSteps{0};
    LabelBox box;
    double score{0.0};
};

inline std::vector<LabelPlacement> optimizeLabelLayout(
    const std::vector<LabelInput>& labels,
    const std::vector<LabelBox>& obstacles,
    int maximumAlongSteps = 3,
    int maximumNormalSteps = 4,
    double padding = 0.0)
{
    std::vector<LabelPlacement> placements(labels.size());
    if (labels.empty()) return placements;
    std::vector<std::size_t> order(labels.size());
    for (std::size_t index = 0; index < labels.size(); ++index) order[index] = index;
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        double collisionsA = 0.0;
        double collisionsB = 0.0;
        for (const LabelInput& other : labels) {
            collisionsA += overlapArea(labels[a].box, other.box, padding);
            collisionsB += overlapArea(labels[b].box, other.box, padding);
        }
        for (const LabelBox& obstacle : obstacles) {
            collisionsA += overlapArea(labels[a].box, obstacle, padding);
            collisionsB += overlapArea(labels[b].box, obstacle, padding);
        }
        return collisionsA > collisionsB;
    });
    std::vector<bool> placed(labels.size(), false);

    const auto candidate = [](const LabelInput& input, int along, int normal) {
        const Point2 axis = normalized(input.axis);
        const Point2 perpendicular{-axis.y, axis.x};
        const Point2 delta = axis * (input.alongStep * static_cast<double>(along)) +
            perpendicular * (input.normalStep * static_cast<double>(normal));
        return translated(input.box, delta);
    };
    const auto scoreCandidate = [&](std::size_t label, int along, int normal, const LabelBox& box) {
        double score = labels[label].movementWeight *
            (std::abs(along) * labels[label].alongStep + std::abs(normal) * labels[label].normalStep);
        for (const LabelBox& obstacle : obstacles) score += 1.0e6 * overlapArea(box, obstacle, padding);
        for (std::size_t other = 0; other < labels.size(); ++other) {
            if (other == label || !placed[other]) continue;
            score += 1.0e6 * overlapArea(box, placements[other].box, padding);
        }
        return score;
    };

    for (std::size_t label : order) {
        LabelPlacement best;
        best.score = std::numeric_limits<double>::max();
        for (int normal = -maximumNormalSteps; normal <= maximumNormalSteps; ++normal) {
            for (int along = -maximumAlongSteps; along <= maximumAlongSteps; ++along) {
                const LabelBox box = candidate(labels[label], along, normal);
                const double score = scoreCandidate(label, along, normal, box);
                if (score < best.score) best = {along, normal, box, score};
            }
        }
        placements[label] = best;
        placed[label] = true;
    }

    // A deterministic local-improvement pass removes most greedy ordering artifacts.
    for (std::size_t pass = 0; pass < 3; ++pass) {
        bool changed = false;
        for (std::size_t label = 0; label < labels.size(); ++label) {
            placed[label] = false;
            LabelPlacement best = placements[label];
            best.score = std::numeric_limits<double>::max();
            for (int normal = -maximumNormalSteps; normal <= maximumNormalSteps; ++normal) {
                for (int along = -maximumAlongSteps; along <= maximumAlongSteps; ++along) {
                    const LabelBox box = candidate(labels[label], along, normal);
                    const double score = scoreCandidate(label, along, normal, box);
                    if (score < best.score) best = {along, normal, box, score};
                }
            }
            placed[label] = true;
            if (best.alongSteps != placements[label].alongSteps || best.normalSteps != placements[label].normalSteps) {
                placements[label] = best;
                changed = true;
            }
        }
        if (!changed) break;
    }
    return placements;
}

struct CoverageCandidate {
    std::vector<std::size_t> features;
    double cost{1.0};
    std::size_t sourceIndex{0};
};

// Greedy weighted set cover followed by redundancy pruning. It is deterministic,
// dependency-free, and suitable for the small candidate sets produced by a CAD object.
inline std::vector<std::size_t> selectCoveringCandidates(
    std::size_t featureCount,
    const std::vector<CoverageCandidate>& candidates,
    std::size_t maximumSelected = std::numeric_limits<std::size_t>::max())
{
    std::vector<bool> covered(featureCount, false);
    std::size_t uncovered = featureCount;
    std::vector<std::size_t> selected;
    while (uncovered > 0 && selected.size() < maximumSelected) {
        std::size_t best = candidates.size();
        double bestRatio = 0.0;
        for (std::size_t index = 0; index < candidates.size(); ++index) {
            std::size_t gain = 0;
            for (std::size_t feature : candidates[index].features) {
                if (feature < featureCount && !covered[feature]) ++gain;
            }
            const double ratio = gain / std::max(candidates[index].cost, 1e-9);
            if (gain > 0 && ratio > bestRatio) {
                bestRatio = ratio;
                best = index;
            }
        }
        if (best == candidates.size()) break;
        selected.push_back(best);
        for (std::size_t feature : candidates[best].features) {
            if (feature < featureCount && !covered[feature]) {
                covered[feature] = true;
                --uncovered;
            }
        }
    }

    for (std::size_t position = selected.size(); position-- > 0;) {
        std::vector<std::size_t> counts(featureCount, 0);
        for (std::size_t selectedIndex : selected) {
            for (std::size_t feature : candidates[selectedIndex].features) {
                if (feature < featureCount) ++counts[feature];
            }
        }
        bool redundant = true;
        for (std::size_t feature : candidates[selected[position]].features) {
            if (feature < featureCount && counts[feature] <= 1) { redundant = false; break; }
        }
        if (redundant) selected.erase(selected.begin() + static_cast<std::ptrdiff_t>(position));
    }
    return selected;
}

// Selects a compact, non-redundant sample of path edges. Endpoints and direction
// families are treated as features, while longer edges are cheaper candidates.
inline std::vector<std::size_t> selectRepresentativeSegments(
    const std::vector<Segment2>& segments,
    std::size_t maximumSelected,
    const TolerancePolicy& tolerance = {})
{
    if (segments.empty() || maximumSelected == 0) return {};
    const double epsilon = tolerance.linear(geometryScale(segments));
    SpatialPointIndex points(epsilon);
    std::vector<std::pair<std::size_t, std::size_t>> endpoints;
    endpoints.reserve(segments.size());
    double maximumLength = 0.0;
    for (const Segment2& segment : segments) {
        endpoints.push_back({points.insertOrFind(segment.start), points.insertOrFind(segment.end)});
        maximumLength = std::max(maximumLength, length(segment.end - segment.start));
    }
    constexpr std::size_t kDirectionFamilies = 8;
    const std::size_t directionFeatureBase = points.points().size();
    std::vector<CoverageCandidate> candidates;
    candidates.reserve(segments.size());
    for (std::size_t index = 0; index < segments.size(); ++index) {
        const Point2 delta = segments[index].end - segments[index].start;
        const double segmentLength = length(delta);
        if (segmentLength <= epsilon) continue;
        double angle = normalizeAngle(std::atan2(delta.y, delta.x));
        if (angle >= kPi) angle -= kPi;
        const std::size_t direction = std::min<std::size_t>(
            kDirectionFamilies - 1,
            static_cast<std::size_t>(angle / kPi * static_cast<double>(kDirectionFamilies)));
        candidates.push_back({
            {endpoints[index].first, endpoints[index].second, directionFeatureBase + direction},
            maximumLength / segmentLength,
            index,
        });
    }
    const auto selectedCandidates = selectCoveringCandidates(
        directionFeatureBase + kDirectionFamilies,
        candidates,
        maximumSelected);
    std::vector<std::size_t> result;
    result.reserve(selectedCandidates.size());
    for (std::size_t candidate : selectedCandidates) result.push_back(candidates[candidate].sourceIndex);
    return result;
}

} // namespace vwad::algo
