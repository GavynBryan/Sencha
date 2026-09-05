#pragma once

#include <assets/data/DataAssetTypeRegistry.h>
#include <core/metadata/DataSchema.h>

#include <string>

// Game-wide authored choices the module reads at spawn time: which cooked
// scenes stand in for the archetypes the code would otherwise build by hand.
// Every field is optional; an empty path means "use the procedural spawn", so
// a project adopts prefabs one archetype at a time.
struct CompiledArenaSettings
{
    std::string PlayerPawnScenePath; // asset://...smap, or empty
    std::string TurretScenePath;     // asset://...smap, or empty
};

void RegisterArenaSettingsData(DataAssetTypeRegistry& types,
                              DataSchemaRegistry& schemas);
void UnregisterArenaSettingsData(DataAssetTypeRegistry& types,
                                DataSchemaRegistry& schemas);
