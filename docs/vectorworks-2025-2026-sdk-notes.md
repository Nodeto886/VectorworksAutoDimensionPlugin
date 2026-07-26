# Vectorworks 2025/2026 SDK Notes

## 2025

- Build as a Vectorworks SDK plug-in module.
- Put the built module or shortcut/alias under Vectorworks `Plug-ins`.
- No external TCP service is required.

## 2026

- Same C++ source strategy as 2025.
- Vectorworks 2026 introduces plugin credential requirements for SDK plugins.
- Fill `resources/AutoDimensionPlugin.credentials.json.template` using your developer information and package it next to the SDK plugin as required by the official credential workflow.

## Auto-dimension entry points

The official Auto-dimension support path is the Plug-in Object event sink:

- `OnAutoDimMessage_GetLocalizedTypeName`
- `OnAutoDimMessage_GetSupportedTypes`
- `OnAutoDimMessage_GetDimensionDefinitions`

The dimensions are returned as `TAutoDimensionDefinition` values. Vectorworks / Graphic Legend then owns the actual display and update flow.

## Important distinction

This plugin should not use a long-running TCP process. TCP can still exist in a different product for data exchange, but automatic dimensions should stay inside Vectorworks and be produced by SDK events.

## Runtime API diagnostics

The release builds include a temporary in-process diagnostic trace for API validation:

- Vectorworks 2025: `vw-autodim-runtime-2025.txt`
- Vectorworks 2026: `vw-autodim-runtime-2026.txt`

The trace records picked object type and UUID, 3D bounds, proxy creation, object duplication, container insertion, Graphic Legend view requests, supported dimension counts, and returned dimension points. The direct tool also logs traversed object, point, straight-segment, and detail-segment counts, truncation, dominant axis, oriented extents, and every created dimension handle.

## Validation matrix

- 2D line: horizontal, vertical, diagonal, rotated, and zero-length.
- Open and closed 2D geometry: polygon, polyline, arc, rectangle, oval, and grouped geometry.
- Symbols: rotated, mirrored, non-uniformly scaled, nested, and hybrid 2D/3D symbols.
- Spotlight: Lighting Device objects with accessories and multiple view components.
- 3D geometry: extrusion, sweep, mesh, generic solid, and rotated objects away from the ground plane.
- Graphic Legend views: Top/Plan, Top, Front, Back, Left, and Right.
- Direct tool views: Top/Plan and Top keep the ground plane, Front, Back, Left, and Right measure the real Z range, and the isometric views fall back to the ground plane.
- Working plane lifecycle: the plane in use before an elevation run is restored afterwards, including runs that create no dimension at all.
- Lifecycle: source edit, source delete, duplicate, undo/redo, document reopen, layer/class visibility, and broken UUID recovery.
- Performance: repeated Graphic Legend resets and large selections without recursive proxy creation.

For the first interactive test, select the source object before activating the tool. The ordinary tool creates linear dimension objects directly and does not duplicate the source object; the click path remains available for objects that are not preselected.

The ordinary tool now treats a 2D line as a special measured primitive. It reads the line's real endpoints with `GetEndPoints`, creates horizontal and vertical projection dimensions, creates an aligned true-length dimension with `CreateLinearDimension`, and creates the acute angle from the horizontal reference with `CreateAngleDimension`.

The direct tool uses `ForEachPolyEdge` for polygon and polyline edges, `FirstMemberObj` for groups and parametric generated geometry, and `GetDefinition` plus entity matrices for nested symbol instances. Open paths add at most three longest straight-edge dimensions. Closed paths, groups, symbols, and lighting devices add oriented overall width, height, and a dominant-axis angle only when the derived axis is meaningfully rotated.

Overall projected width and height remain the fallback for curves, meshes, generic solids, and geometry that reaches a traversal limit. Lighting-field-specific measurement points remain a separate rule.

## View-dependent measuring plane

`GetCurrentView` returns a `TStandardView`. `standardViewFront`, `standardViewBack`, `standardViewLeft` and `standardViewRight` switch the tool to elevation measuring; every other value, including Top/Plan and the isometric views, keeps the ground-plane behaviour.

An elevation run builds an `Axis` whose `i` is screen right, `j` is world Z and `k` points at the camera, with the vertex on the face of the batch bounding cube that faces the camera. The tool then calls `GetWorkingPlane` to remember the user plane, `NewWorkingPlane` to install the measuring plane, and `GetWorkingPlanePlanarRefID` to get the `TPlanarRefID` that every created dimension is tagged with through `SetPlanarRefID`. `NewWorkingPlane` restores the saved plane once the batch is finished, and the whole plane switch happens outside the per-object undo events.

Model points are converted with `ModelPtToPlanarPt`; the plain dot product against the axis vectors is the fallback when that call fails. The Z range comes from `GetObjectCube`, which is the only 3D bounds call in the SDK, so a rotated symbol or lighting device reports the height of its bounding cube. The in-plane horizontal range is tightened with the traversed 2D geometry because the plane axes never carry a Z component.

If `GetWorkingPlanePlanarRefID` returns 0 the dimensions would silently land on the ground plane and read as a degenerate line in the elevation, so the tool restores the working plane and falls back to the documented plan behaviour. The runtime trace records this as `planarRefFailed=true`.
