#pragma once

#include <assets/cook/CookDiagnostic.h>
#include <core/json/JsonValue.h>
#include <navigation/NavTileBuild.h>

#include <string>
#include <string_view>
#include <vector>

//=============================================================================
// Navigation settings
//
// The project's navigation build configuration, authored as a data asset of
// subtype "navigation.settings":
//
//   { "type": "navigation.settings", "version": 1,
//     "data": {
//       "profiles": [ { "tag": "navigation.profile.humanoid", "radius": 0.3,
//                       "height": 1.8, "max_slope_degrees": 45,
//                       "max_climb": 0.35, "cell_size": 0.15,
//                       "cell_height": 0.1, "tile_cells": 32 } ],
//       "areas": [ "navigation.area.water" ] } }
//
// Only the cook reads it. Everything the runtime needs is copied into each
// zone's cooked navigation file, so the settings never ship.
//=============================================================================

inline constexpr std::string_view kNavigationSettingsSubtype = "navigation.settings";
inline constexpr std::string_view kNavigationDefaultAreaName = "navigation.area.default";

struct NavigationProfileSetting
{
    std::string Name;
    NavBuildProfile Build;
};

struct NavigationSettings
{
    std::vector<NavigationProfileSetting> Profiles;
    // Authored area names; backend area index i + 1 names Areas[i].
    std::vector<std::string> Areas;
};

struct DataSchema;

// The shape of a settings asset's "data" object.
[[nodiscard]] const DataSchema& NavigationSettingsSchema();

// Parses and validates a settings envelope. Returns false and appends
// diagnostics when the envelope or any value is invalid.
[[nodiscard]] bool ParseNavigationSettings(const JsonValue& envelope,
                                           NavigationSettings& settings,
                                           std::vector<CookDiagnostic>& diagnostics);
