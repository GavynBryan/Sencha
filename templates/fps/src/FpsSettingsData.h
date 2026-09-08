#pragma once

#include <assets/data/DataAssetTypeRegistry.h>
#include <core/metadata/DataSchema.h>

#include <string>

// Game-wide authored choices the module reads at spawn time: which cooked
// scene is spawned as a player's body. Empty means no body; a game with no
// pawn content is a game that is not set up yet, and says so.
struct CompiledGameSettings
{
    std::string PlayerPawnScenePath; // asset://...smap, or empty
};

void RegisterGameSettingsData(DataAssetTypeRegistry& types,
                              DataSchemaRegistry& schemas);
void UnregisterGameSettingsData(DataAssetTypeRegistry& types,
                                DataSchemaRegistry& schemas);
