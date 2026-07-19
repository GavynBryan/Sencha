#include "WorldCook.h"

#include "DocumentCook.h"
#include "WorldDocument.h"

#include <core/json/JsonStringify.h>
#include <core/logging/Logger.h>
#include <core/logging/LoggingProvider.h>

#include <fstream>
#include <system_error>

WorldCookResult CookWorld(WorldDocument& world,
                          const std::filesystem::path& assetsRoot,
                          double cellSize,
                          LoggingProvider& logging,
                          RuntimeAssets* assets,
                          const LightingCookParams& lightmapParams)
{
    namespace fs = std::filesystem;
    WorldCookResult result;
    auto& log = logging.GetLogger<WorldDocument>();

    if (!world.IsWorld())
    {
        result.Error = "CookWorld: no world is open";
        return result;
    }

    // Saved files only: a dirty document or an unresolved scene means the
    // authored files on disk do not describe the world the designer sees.
    std::string blocked;
    world.VisitOpenZones([&](ZoneId, EditorDocument& document, const ZoneViewState&)
                         {
                             if (!document.IsDirty())
                                 return;
                             if (!blocked.empty())
                                 blocked += ", ";
                             blocked += document.GetDisplayName();
                         });
    if (world.WorldSceneDocument().IsDirty())
    {
        if (!blocked.empty())
            blocked += ", ";
        blocked += "world scene";
    }
    if (!blocked.empty())
    {
        result.Error = "CookWorld: unsaved zone documents: " + blocked;
        return result;
    }
    for (const ZoneHeader& zone : world.Manifest().Zones)
    {
        std::error_code ec;
        if (!zone.SceneRef.empty() && fs::exists(world.ResolveScenePath(zone.SceneRef), ec))
            continue;
        if (!blocked.empty())
            blocked += ", ";
        blocked += zone.Name;
    }
    if (!blocked.empty())
    {
        result.Error = "CookWorld: zones without a saved scene: " + blocked;
        return result;
    }

    // The cooked manifest starts as the authored records; each zone cook fills
    // the cooked-only fields.
    WorldPartitionManifest cooked = world.Manifest();
    for (ZoneHeader& zone : cooked.Zones)
    {
        const fs::path scenePath = world.ResolveScenePath(zone.SceneRef);
        const DocumentCookResult zoneCook =
            CookDocument(scenePath, assetsRoot, cellSize, &logging, assets,
                         lightmapParams);
        if (!zoneCook.Success)
        {
            result.Error = "CookWorld: zone '" + zone.Name + "': " + zoneCook.Error;
            return result;
        }

        std::error_code ec;
        zone.CookedSceneRef = fs::relative(zoneCook.CookedScenePath, assetsRoot, ec).generic_string();
        zone.CookedCollisionRef =
            fs::relative(zoneCook.CollisionSidecarPath, assetsRoot, ec).generic_string();
        zone.CookedContentHash = zoneCook.ContentHash;
    }

    // The world scene cooks through the same level-cook path as a zone scene;
    // its cooked refs land at the manifest's top level. A world saved before
    // the world scene existed has no ref and cooks without one.
    if (!cooked.WorldSceneRef.empty())
    {
        const fs::path scenePath = world.ResolveScenePath(cooked.WorldSceneRef);
        std::error_code ec;
        if (!fs::exists(scenePath, ec))
        {
            result.Error = "CookWorld: world scene '" + cooked.WorldSceneRef
                + "' has no saved file; save the world first";
            return result;
        }
        const DocumentCookResult sceneCook =
            CookDocument(scenePath, assetsRoot, cellSize, &logging, assets,
                         lightmapParams);
        if (!sceneCook.Success)
        {
            result.Error = "CookWorld: world scene: " + sceneCook.Error;
            return result;
        }
        cooked.CookedWorldSceneRef =
            fs::relative(sceneCook.CookedScenePath, assetsRoot, ec).generic_string();
        cooked.CookedWorldCollisionRef =
            fs::relative(sceneCook.CollisionSidecarPath, assetsRoot, ec).generic_string();
        cooked.CookedWorldContentHash = sceneCook.ContentHash;
    }

    const std::string worldStem = fs::path(std::string(world.WorldPath())).stem().string();
    const fs::path cookedDir = assetsRoot / ".cooked/worlds";
    std::error_code ec;
    fs::create_directories(cookedDir, ec);
    const fs::path manifestPath = cookedDir / (worldStem + ".sworld.json");

    std::ofstream file(manifestPath, std::ios::binary | std::ios::trunc);
    if (!file.is_open())
    {
        result.Error = "CookWorld: cannot write '" + manifestPath.generic_string() + "'";
        return result;
    }
    file << JsonStringify(WriteWorldPartitionManifest(cooked), /*pretty*/ true);
    if (!file.good())
    {
        result.Error = "CookWorld: write failed for '" + manifestPath.generic_string() + "'";
        return result;
    }

    log.Info("cooked world '{}': {} zones -> {}", cooked.Name, cooked.Zones.size(),
             manifestPath.generic_string());
    result.Success = true;
    result.CookedManifestPath = manifestPath;
    result.ZoneCount = cooked.Zones.size();
    return result;
}
