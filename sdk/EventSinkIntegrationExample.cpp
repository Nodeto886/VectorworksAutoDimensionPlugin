/*
    Example integration for a Vectorworks SDK Plug-in Object.

    This file shows where to call the standalone adapter. It is intentionally
    not compiled by this repository's default CMake target because a real
    Vectorworks SDK project must provide the EventSink class, object handle,
    bounding box extraction, and module registration.
*/

#include "../src/Vectorworks/AutoDimensionEventSink.h"

bool CYourObject_EventSink::OnAutoDimMessage_GetLocalizedTypeName(
    const TXString& instrDimID_UniversalName,
    TXString& outstrDimID_LocalizedName)
{
    return vwad::vw::getLocalizedTypeName(
        instrDimID_UniversalName,
        outstrDimID_LocalizedName);
}

bool CYourObject_EventSink::OnAutoDimMessage_GetSupportedTypes(
    EViewTypes inView,
    std::vector<VectorWorks::Extension::TAutoDimensionTypeInfo>& outvecTypes)
{
    return vwad::vw::getSupportedTypes(inView, outvecTypes);
}

bool CYourObject_EventSink::OnAutoDimMessage_GetDimensionDefinitions(
    EViewTypes inView,
    const TXString& instrDimID_UniversalName,
    VectorWorks::Extension::EAutoDimensionPlacement inPlace,
    std::vector<VectorWorks::Extension::TAutoDimensionDefinition>& outvecDimensions)
{
    vwad::Bounds3 bounds;

    // Replace this placeholder with SDK calls against the current plug-in
    // object's real 2D/3D geometry or object bounds.
    bounds.minX = 0.0;
    bounds.minY = 0.0;
    bounds.minZ = 0.0;
    bounds.maxX = 1000.0;
    bounds.maxY = 500.0;
    bounds.maxZ = 250.0;

    return vwad::vw::getDimensionDefinitions(
        inView,
        instrDimID_UniversalName,
        inPlace,
        bounds,
        outvecDimensions);
}
