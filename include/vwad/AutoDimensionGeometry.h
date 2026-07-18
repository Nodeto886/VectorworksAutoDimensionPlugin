#pragma once

#include <string>
#include <vector>

namespace vwad {

struct Point3 {
    double x{0.0};
    double y{0.0};
    double z{0.0};
};

struct Bounds3 {
    double minX{0.0};
    double minY{0.0};
    double minZ{0.0};
    double maxX{0.0};
    double maxY{0.0};
    double maxZ{0.0};

    [[nodiscard]] bool isValid() const;
};

enum class DimensionPlacement {
    Top,
    Bottom,
    Left,
    Right,
};

enum class DimensionType {
    OverallWidth,
    OverallHeight,
    OverallDepth,
};

struct DimensionTypeInfo {
    std::string universalName;
    std::string localizedName;
    DimensionPlacement placement{DimensionPlacement::Bottom};
};

struct DimensionDefinition {
    std::string universalName;
    DimensionPlacement placement{DimensionPlacement::Bottom};
    Point3 start;
    Point3 end;
    int offsetIndex{0};
};

[[nodiscard]] const std::vector<DimensionTypeInfo>& supportedDimensionTypes();
[[nodiscard]] std::string universalName(DimensionType type);
[[nodiscard]] std::string localizedName(const std::string& universalName);

[[nodiscard]] std::vector<DimensionDefinition> buildDimensionDefinitions(
    const Bounds3& bounds,
    const std::string& universalName,
    DimensionPlacement placement);

} // namespace vwad
