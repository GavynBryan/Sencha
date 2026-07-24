#include "WorldCook.h"

#include "CookArtifactTransaction.h"
#include "EditorDocument.h"
#include "WorldDocument.h"

#include <core/json/JsonStringify.h>
#include <core/logging/Logger.h>
#include <core/logging/LoggingProvider.h>
#include <zone/WorldPartitionIds.h>

#include <fstream>
#include <span>
#include <system_error>
#include <utility>
#include <vector>

struct WorldCookInput::Data
{
    struct Document
    {
        std::size_t ZoneIndex = static_cast<std::size_t>(-1);
        std::string Label;
        std::string LevelName;
        std::string SourceRel;
        DocumentCookInput Input;
    };

    WorldPartitionManifest Manifest;
    std::string WorldStem;
    std::vector<Document> Documents;
};

WorldCookInput::WorldCookInput(std::unique_ptr<Data> data)
    : Input(std::move(data))
{
}

WorldCookInput::WorldCookInput(WorldCookInput&&) noexcept = default;
WorldCookInput& WorldCookInput::operator=(WorldCookInput&&) noexcept = default;
WorldCookInput::~WorldCookInput() = default;

std::optional<WorldCookInput> CollectWorldCookInput(
    WorldDocument& world,
    const std::filesystem::path& assetsRoot,
    double cellSize,
    LoggingProvider& logging,
    RuntimeAssets* assets,
    const LightingCookParams& lightmapParams,
    const DocumentCookOptions& options,
    std::string* error)
{
    namespace fs = std::filesystem;
    const auto fail = [error](std::string message) -> std::optional<WorldCookInput>
    {
        if (error != nullptr)
            *error = std::move(message);
        return std::nullopt;
    };

    if (!world.IsWorld())
        return fail("CookWorld: no world is open");

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
        return fail("CookWorld: unsaved zone documents: " + blocked);

    auto data = std::make_unique<WorldCookInput::Data>();
    data->Manifest = world.Manifest();
    data->WorldStem = fs::path(std::string(world.WorldPath())).stem().string();

    std::vector<fs::path> scenePaths;
    std::vector<std::unique_ptr<EditorDocument>> documents;
    std::vector<ProbeHaloZone> halos;
    scenePaths.reserve(data->Manifest.Zones.size());
    documents.reserve(data->Manifest.Zones.size());
    halos.reserve(data->Manifest.Zones.size());
    for (const ZoneHeader& zone : data->Manifest.Zones)
    {
        const fs::path scenePath = world.ResolveScenePath(zone.SceneRef);
        std::error_code ec;
        if (zone.SceneRef.empty() || !fs::exists(scenePath, ec))
            return fail("CookWorld: zone '" + zone.Name
                + "' has no saved scene; save the world first");

        auto document = std::make_unique<EditorDocument>(logging);
        if (assets != nullptr)
            document->SetAssetEnvironment(*assets);
        if (!document->Load(scenePath.generic_string()))
            return fail("CookWorld: zone '" + zone.Name
                + "': could not load '" + scenePath.generic_string() + "'");
        std::optional<ProbeHaloZone> halo =
            CollectZoneBakeHalo(scenePath, assetsRoot, &logging, assets);
        if (!halo.has_value())
            return fail("CookWorld: zone '" + zone.Name
                + "': could not collect bake halo");
        scenePaths.push_back(scenePath);
        documents.push_back(std::move(document));
        halos.push_back(std::move(*halo));
    }

    for (std::size_t zoneIndex = 0;
         zoneIndex < data->Manifest.Zones.size(); ++zoneIndex)
    {
        std::swap(halos[zoneIndex], halos.back());
        const std::span<const ProbeHaloZone> neighbors(
            halos.data(), halos.size() - 1);
        std::error_code ec;
        const std::string sourceRel =
            fs::relative(scenePaths[zoneIndex], assetsRoot, ec).generic_string();
        std::string inputError;
        DocumentCookOptions documentOptions = options;
        documentOptions.OutputNamespace = "_world-generations/"
            + data->WorldStem + "/"
            + ZoneIdToString(data->Manifest.Zones[zoneIndex].Id);
        std::optional<DocumentCookInput> input = CollectDocumentCookInput(
            *documents[zoneIndex], assetsRoot, cellSize, logging, assets,
            lightmapParams, neighbors, documentOptions, &inputError);
        std::swap(halos[zoneIndex], halos.back());
        if (!input.has_value())
            return fail("CookWorld: zone '" + data->Manifest.Zones[zoneIndex].Name
                + "': " + inputError);
        data->Documents.push_back(WorldCookInput::Data::Document{
            .ZoneIndex = zoneIndex,
            .Label = data->Manifest.Zones[zoneIndex].Name,
            .LevelName = scenePaths[zoneIndex].stem().generic_string(),
            .SourceRel = sourceRel,
            .Input = std::move(*input),
        });
    }

    if (!data->Manifest.WorldSceneRef.empty())
    {
        const fs::path scenePath = world.ResolveScenePath(data->Manifest.WorldSceneRef);
        std::error_code ec;
        if (!fs::exists(scenePath, ec))
            return fail("CookWorld: world scene '" + data->Manifest.WorldSceneRef
                + "' has no saved file; save the world first");
        EditorDocument document(logging);
        if (assets != nullptr)
            document.SetAssetEnvironment(*assets);
        if (!document.Load(scenePath.generic_string()))
            return fail("CookWorld: could not load world scene '"
                + scenePath.generic_string() + "'");
        std::string inputError;
        DocumentCookOptions documentOptions = options;
        documentOptions.OutputNamespace =
            "_world-generations/" + data->WorldStem + "/world_scene";
        std::optional<DocumentCookInput> input = CollectDocumentCookInput(
            document, assetsRoot, cellSize, logging, assets, lightmapParams,
            {}, documentOptions, &inputError);
        if (!input.has_value())
            return fail("CookWorld: world scene: " + inputError);
        data->Documents.push_back(WorldCookInput::Data::Document{
            .Label = "world scene",
            .LevelName = scenePath.stem().generic_string(),
            .SourceRel = fs::relative(scenePath, assetsRoot, ec).generic_string(),
            .Input = std::move(*input),
        });
    }
    return WorldCookInput(std::move(data));
}

WorldCookResult ExecuteWorldCook(WorldCookInput input,
                                 const std::filesystem::path& assetsRoot,
                                 LoggingProvider& logging)
{
    namespace fs = std::filesystem;
    WorldCookResult result;
    if (input.Input == nullptr)
    {
        result.Error = "CookWorld: empty cook input";
        return result;
    }

    WorldPartitionManifest& cooked = input.Input->Manifest;
    for (WorldCookInput::Data::Document& document : input.Input->Documents)
    {
        DocumentCookResult cookedDocument = ExecuteDocumentCook(
            std::move(document.Input), document.LevelName, document.SourceRel,
            assetsRoot, logging);
        if (!cookedDocument.Success)
        {
            result.Error = "CookWorld: " + document.Label + ": "
                + cookedDocument.Error;
            return result;
        }

        std::error_code ec;
        const std::string sceneRef = fs::relative(
            cookedDocument.CookedScenePath, assetsRoot, ec).generic_string();
        const std::string collisionRef = fs::relative(
            cookedDocument.CollisionSidecarPath, assetsRoot, ec).generic_string();
        if (document.ZoneIndex != static_cast<std::size_t>(-1))
        {
            ZoneHeader& zone = cooked.Zones[document.ZoneIndex];
            zone.CookedSceneRef = sceneRef;
            zone.CookedCollisionRef = collisionRef;
            zone.CookedContentHash = cookedDocument.ContentHash;
        }
        else
        {
            cooked.CookedWorldSceneRef = sceneRef;
            cooked.CookedWorldCollisionRef = collisionRef;
            cooked.CookedWorldContentHash = cookedDocument.ContentHash;
        }
    }

    const fs::path manifestPath = assetsRoot / ".cooked/worlds"
        / (input.Input->WorldStem + ".sworld.json");
    CookArtifactTransaction publication(assetsRoot,
        "worlds/" + input.Input->WorldStem);
    std::ofstream file(publication.Stage(manifestPath),
                       std::ios::binary | std::ios::trunc);
    if (!file.is_open())
    {
        result.Error = "CookWorld: cannot stage '" + manifestPath.generic_string() + "'";
        return result;
    }
    file << JsonStringify(WriteWorldPartitionManifest(cooked), true);
    file.close();
    if (!file.good())
    {
        result.Error = "CookWorld: write failed for '" + manifestPath.generic_string() + "'";
        return result;
    }
    std::string publishError;
    if (!publication.Commit(&publishError))
    {
        result.Error = "CookWorld: " + publishError;
        return result;
    }

    logging.GetLogger<WorldDocument>().Info(
        "cooked world '{}': {} zones -> {}", cooked.Name, cooked.Zones.size(),
        manifestPath.generic_string());
    result.Success = true;
    result.CookedManifestPath = manifestPath;
    result.ZoneCount = cooked.Zones.size();
    return result;
}

WorldCookResult CookWorld(WorldDocument& world,
                          const std::filesystem::path& assetsRoot,
                          double cellSize,
                          LoggingProvider& logging,
                          RuntimeAssets* assets,
                          const LightingCookParams& lightmapParams,
                          const DocumentCookOptions& options)
{
    std::string error;
    std::optional<WorldCookInput> input = CollectWorldCookInput(
        world, assetsRoot, cellSize, logging, assets, lightmapParams, options,
        &error);
    if (!input.has_value())
    {
        WorldCookResult result;
        result.Error = std::move(error);
        return result;
    }
    return ExecuteWorldCook(std::move(*input), assetsRoot, logging);
}
