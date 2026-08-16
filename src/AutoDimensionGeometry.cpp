#include "vwad/AutoDimensionGeometry.h"

#include <cmath>

namespace vwad {
namespace {

constexpr const char* kOverallWidth = "AD_OverallWidth";
constexpr const char* kOverallHeight = "AD_OverallHeight";
constexpr const char* kOverallDepth = "AD_OverallDepth";

const std::vector<DimensionTypeInfo> kSupportedTypes = {
    {kOverallWidth, "Overall Width", DimensionPlacement::Bottom},
    {kOverallHeight, "Overall Height", DimensionPlacement::Right},
    {kOverallDepth, "Overall Depth", DimensionPlacement::Top},
};

} // namespace

bool Bounds3::isValid() const
{
    const bool finite =
        std::isfinite(minX) && std::isfinite(minY) && std::isfinite(minZ) &&
        std::isfinite(maxX) && std::isfinite(maxY) && std::isfinite(maxZ);
    if (!finite || minX > maxX || minY > maxY || minZ > maxZ) {
        return false;
    }

    return maxX > minX || maxY > minY || maxZ > minZ;
}

const std::vector<DimensionTypeInfo>& supportedDimensionTypes()
{
    return kSupportedTypes;
}

std::string universalName(DimensionType type)
{
    switch (type) {
    case DimensionType::OverallWidth:
        return kOverallWidth;
    case DimensionType::OverallHeight:
        return kOverallHeight;
    case DimensionType::OverallDepth:
        return kOverallDepth;
    }

    return {};
}

std::string localizedName(const std::string& name)
{
    for (const auto& typeInfo : kSupportedTypes) {
        if (typeInfo.universalName == name) {
            return typeInfo.localizedName;
        }
    }

    return {};
}

std::vector<DimensionDefinition> buildDimensionDefinitions(
    const Bounds3& bounds,
    const std::string& name,
    DimensionPlacement placement)
{
    if (!bounds.isValid()) {
        return {};
    }

    DimensionDefinition definition;
    definition.universalName = name;
    definition.placement = placement;
    definition.offsetIndex = 0;

    if (name == kOverallWidth && bounds.maxX > bounds.minX) {
        definition.start = {bounds.minX, bounds.minY, bounds.minZ};
        definition.end = {bounds.maxX, bounds.minY, bounds.minZ};
        definition.placement = DimensionPlacement::Bottom;
        return {definition};
    }

    if (name == kOverallHeight && bounds.maxY > bounds.minY) {
        definition.start = {bounds.maxX, bounds.minY, bounds.minZ};
        definition.end = {bounds.maxX, bounds.maxY, bounds.minZ};
        definition.placement = DimensionPlacement::Right;
        return {definition};
    }

    if (name == kOverallDepth && bounds.maxZ > bounds.minZ) {
        definition.start = {bounds.minX, bounds.maxY, bounds.minZ};
        definition.end = {bounds.minX, bounds.maxY, bounds.maxZ};
        definition.placement = DimensionPlacement::Top;
        return {definition};
    }

    return {};
}

} // namespace vwad
