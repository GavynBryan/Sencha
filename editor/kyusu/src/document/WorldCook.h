#pragma once

#include <assets/cook/LightmapCookParams.h>

#include <filesystem>
#include <string>

class WorldDocument;
class LoggingProvider;
struct RuntimeAssets;

struct WorldCookResult
{
    bool                  Success = false;
    std::string           Error;
    std::filesystem::path CookedManifestPath;
    std::size_t           ZoneCount = 0;
};

// Cooks every zone of a saved world through the file-based level cook and
// writes the cooked world manifest (.cooked/worlds/<world>.sworld.json): the
// authored records plus per-zone cooked scene path, collision sidecar path,
// and the content hash the cooked cache keyed that zone's cook by. Cooks saved
// files only (no live-snapshot path): refuses, naming the zones, when any zone
// document is dirty or any scene ref resolves to no file. Adds no cook
// mechanics and no second cache; unchanged zones re-cook as cheaply as the
// cooked cache already makes them.
[[nodiscard]] WorldCookResult CookWorld(WorldDocument& world,
                                        const std::filesystem::path& assetsRoot,
                                        double cellSize,
                                        LoggingProvider& logging,
                                        RuntimeAssets* assets,
                                        const LightmapCookParams& lightmapParams = {});
