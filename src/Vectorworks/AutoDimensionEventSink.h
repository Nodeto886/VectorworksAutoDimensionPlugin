#pragma once

#include "vwad/AutoDimensionGeometry.h"

#include <vector>

// Include this header from a real Vectorworks SDK project where the SDK types
// below are available.
//
// Expected SDK types:
// - TXString
// - EViewTypes
// - WorldPt3
// - VectorWorks::Extension::EAutoDimensionPlacement
// - VectorWorks::Extension::TAutoDimensionTypeInfo
// - VectorWorks::Extension::TAutoDimensionDefinition

namespace vwad::vw {

bool getLocalizedTypeName(
    const TXString& universalName,
    TXString& localizedName);

bool getSupportedTypes(
    EViewTypes view,
    std::vector<VectorWorks::Extension::TAutoDimensionTypeInfo>& outTypes);

bool getDimensionDefinitions(
    EViewTypes view,
    const TXString& universalName,
    VectorWorks::Extension::EAutoDimensionPlacement placement,
    const vwad::Bounds3& objectBounds,
    std::vector<VectorWorks::Extension::TAutoDimensionDefinition>& outDefinitions);

} // namespace vwad::vw
