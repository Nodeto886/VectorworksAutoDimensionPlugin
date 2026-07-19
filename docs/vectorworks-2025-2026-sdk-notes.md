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

The trace records picked object type and UUID, 3D bounds, proxy creation, object duplication, container insertion, Graphic Legend view requests, supported dimension counts, and returned dimension points.

## Validation matrix

- 2D line: horizontal, vertical, diagonal, rotated, and zero-length.
- Open and closed 2D geometry: polygon, polyline, arc, rectangle, oval, and grouped geometry.
- Symbols: rotated, mirrored, non-uniformly scaled, nested, and hybrid 2D/3D symbols.
- Spotlight: Lighting Device objects with accessories and multiple view components.
- 3D geometry: extrusion, sweep, mesh, generic solid, and rotated objects away from the ground plane.
- Graphic Legend views: Top/Plan, Top, Front, Back, Left, and Right.
- Lifecycle: source edit, source delete, duplicate, undo/redo, document reopen, layer/class visibility, and broken UUID recovery.
- Performance: repeated Graphic Legend resets and large selections without recursive proxy creation.

For the first interactive test, select the source object before activating the tool. The ordinary tool creates linear dimension objects directly and does not duplicate the source object; the click path remains available for objects that are not preselected.

The ordinary tool now treats a 2D line as a special measured primitive. It reads the line's real endpoints with `GetEndPoints`, creates horizontal and vertical projection dimensions, creates an aligned true-length dimension with `CreateLinearDimension`, and creates the acute angle from the horizontal reference with `CreateAngleDimension`.

Overall width, height, and depth remain the fallback for symbols, lighting devices, open/closed 2D geometry, and 3D objects. Polyline vertex chains, rotated-symbol local axes, and lighting-specific dimensions remain separate geometry rules.
