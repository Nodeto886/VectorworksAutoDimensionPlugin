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
