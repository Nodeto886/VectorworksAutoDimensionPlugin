#include "AutoDimensionEventSink.h"

namespace vwad::vw {
namespace {

TXString toTXString(const std::string& value)
{
    return TXString(value.c_str());
}

VectorWorks::Extension::EAutoDimensionPlacement toVwPlacement(DimensionPlacement placement)
{
    using VectorWorks::Extension::EAutoDimensionPlacement;

    switch (placement) {
    case DimensionPlacement::Top:
        return EAutoDimensionPlacement::eHorizontalTop;
    case DimensionPlacement::Bottom:
        return EAutoDimensionPlacement::eHorizontalBottom;
    case DimensionPlacement::Left:
        return EAutoDimensionPlacement::eVerticalLeft;
    case DimensionPlacement::Right:
        return EAutoDimensionPlacement::eVerticalRight;
    }

    return EAutoDimensionPlacement::eHorizontalBottom;
}

WorldPt3 toWorldPt3(const Point3& point)
{
    return WorldPt3(point.x, point.y, point.z);
}

} // namespace

bool getLocalizedTypeName(const TXString& universalName, TXString& outLocalizedName)
{
    const std::string key = universalName.operator const char*();
    const std::string label = vwad::localizedName(key);
    if (label.empty()) {
        return false;
    }

    outLocalizedName = toTXString(label);
    return true;
}

bool getSupportedTypes(
    EViewTypes view,
    std::vector<VectorWorks::Extension::TAutoDimensionTypeInfo>& outTypes)
{
    (void)view;
    outTypes.clear();

    for (const auto& typeInfo : supportedDimensionTypes()) {
        VectorWorks::Extension::TAutoDimensionTypeInfo vwType;
        vwType.SetID(toTXString(typeInfo.universalName), toTXString(typeInfo.localizedName));
        vwType.fvecPlaces.push_back(toVwPlacement(typeInfo.placement));
        outTypes.push_back(vwType);
    }

    return !outTypes.empty();
}

bool getDimensionDefinitions(
    EViewTypes view,
    const TXString& universalName,
    VectorWorks::Extension::EAutoDimensionPlacement placement,
    const vwad::Bounds3& objectBounds,
    std::vector<VectorWorks::Extension::TAutoDimensionDefinition>& outDefinitions)
{
    (void)view;
    (void)placement;
    outDefinitions.clear();

    const std::string key = universalName.operator const char*();
    const auto definitions = buildDimensionDefinitions(
        objectBounds,
        key,
        DimensionPlacement::Bottom);

    for (const auto& definition : definitions) {
        VectorWorks::Extension::TAutoDimensionDefinition vwDefinition;
        vwDefinition.Set(
            toWorldPt3(definition.start),
            toWorldPt3(definition.end),
            static_cast<short>(definition.offsetIndex));
        outDefinitions.push_back(vwDefinition);
    }

    return !outDefinitions.empty();
}

} // namespace vwad::vw
