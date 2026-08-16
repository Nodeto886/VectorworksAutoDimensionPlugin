#include "vwad/AutoDimensionGeometry.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <string>

namespace {

int failures = 0;

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

bool samePoint(const vwad::Point3& point, double x, double y, double z)
{
    return point.x == x && point.y == y && point.z == z;
}

void testNamesAndSupportedTypes()
{
    const auto& types = vwad::supportedDimensionTypes();
    expect(types.size() == 3, "three dimension types are advertised");
    expect(vwad::universalName(vwad::DimensionType::OverallWidth) == "AD_OverallWidth", "width universal name");
    expect(vwad::universalName(vwad::DimensionType::OverallHeight) == "AD_OverallHeight", "height universal name");
    expect(vwad::universalName(vwad::DimensionType::OverallDepth) == "AD_OverallDepth", "depth universal name");
    expect(vwad::localizedName("AD_OverallWidth") == "Overall Width", "width localized name");
    expect(vwad::localizedName("unknown").empty(), "unknown names do not acquire a label");
}

void testDefinitions()
{
    const vwad::Bounds3 bounds{1.0, 2.0, 3.0, 11.0, 22.0, 33.0};

    const auto width = vwad::buildDimensionDefinitions(
        bounds, "AD_OverallWidth", vwad::DimensionPlacement::Top);
    expect(width.size() == 1, "width definition is created");
    if (!width.empty()) {
        expect(samePoint(width[0].start, 1.0, 2.0, 3.0), "width start point");
        expect(samePoint(width[0].end, 11.0, 2.0, 3.0), "width end point");
        expect(width[0].placement == vwad::DimensionPlacement::Bottom, "width uses its supported placement");
    }

    const auto height = vwad::buildDimensionDefinitions(
        bounds, "AD_OverallHeight", vwad::DimensionPlacement::Bottom);
    expect(height.size() == 1, "height definition is created");
    if (!height.empty()) {
        expect(samePoint(height[0].start, 11.0, 2.0, 3.0), "height start point");
        expect(samePoint(height[0].end, 11.0, 22.0, 3.0), "height end point");
        expect(height[0].placement == vwad::DimensionPlacement::Right, "height uses its supported placement");
    }

    const auto depth = vwad::buildDimensionDefinitions(
        bounds, "AD_OverallDepth", vwad::DimensionPlacement::Bottom);
    expect(depth.size() == 1, "depth definition is created");
    if (!depth.empty()) {
        expect(samePoint(depth[0].start, 1.0, 22.0, 3.0), "depth start point");
        expect(samePoint(depth[0].end, 1.0, 22.0, 33.0), "depth end point");
        expect(depth[0].placement == vwad::DimensionPlacement::Top, "depth uses its supported placement");
    }

    expect(vwad::buildDimensionDefinitions(bounds, "unknown", vwad::DimensionPlacement::Bottom).empty(),
        "unknown types do not create a definition");
}

void testInvalidAndDegenerateBounds()
{
    const vwad::Bounds3 point{4.0, 5.0, 6.0, 4.0, 5.0, 6.0};
    expect(!point.isValid(), "a point is not a measurable bounds");

    const vwad::Bounds3 line{0.0, 5.0, 6.0, 10.0, 5.0, 6.0};
    expect(line.isValid(), "a one-axis range is measurable");
    expect(vwad::buildDimensionDefinitions(line, "AD_OverallWidth", vwad::DimensionPlacement::Bottom).size() == 1,
        "the non-degenerate line axis creates a dimension");
    expect(vwad::buildDimensionDefinitions(line, "AD_OverallHeight", vwad::DimensionPlacement::Right).empty(),
        "a zero-length axis does not create a degenerate dimension");
    expect(vwad::buildDimensionDefinitions(line, "AD_OverallDepth", vwad::DimensionPlacement::Top).empty(),
        "another zero-length axis does not create a degenerate dimension");

    const vwad::Bounds3 reversed{10.0, 0.0, 0.0, 0.0, 20.0, 30.0};
    expect(!reversed.isValid(), "reversed bounds are rejected even when other axes have ranges");
    expect(vwad::buildDimensionDefinitions(reversed, "AD_OverallHeight", vwad::DimensionPlacement::Right).empty(),
        "reversed bounds never produce definitions");

    const double infinity = std::numeric_limits<double>::infinity();
    const vwad::Bounds3 nonFinite{0.0, 0.0, 0.0, infinity, 20.0, 30.0};
    expect(!nonFinite.isValid(), "non-finite bounds are rejected");
    expect(vwad::buildDimensionDefinitions(nonFinite, "AD_OverallWidth", vwad::DimensionPlacement::Bottom).empty(),
        "non-finite bounds never produce definitions");
}

} // namespace

int main()
{
    testNamesAndSupportedTypes();
    testDefinitions();
    testInvalidAndDegenerateBounds();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "All AutoDimensionGeometry tests passed\n";
    return 0;
}
