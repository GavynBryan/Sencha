#include "LevelCook.h"

#include "BrushCookInput.h"
#include "LevelDocument.h"

#include <assets/cook/BrushClustering.h>
#include <assets/cook/BrushGeometryCook.h>
#include <assets/cook/CookedCache.h>
#include <assets/cook/SceneCookOutput.h>
#include <assets/static_mesh/MeshSerializer.h>
#include <core/assets/AssetIdMap.h>
#include <core/hash/ContentHash.h>
#include <core/logging/LoggingProvider.h>
#include <render/static_mesh/MeshGeometry.h>
#include <world/registry/Registry.h>
#include <world/serialization/SceneSerializer.h>

#include <cstdint>
#include <fstream>
#include <span>
#include <string>
#include <unordered_set>
#include <vector>

namespace
{
    // The bake-input fingerprint: exactly the data that reaches the bake (each
    // face's resolved material plus its world-space triangles) and the cell
    // size. Non-geometry component edits don't touch it, so they don't force a
    // re-bake (05-§3); a real geometry/material/transform change does, because
    // the collected triangles are post-transform.
    uint64_t HashBrushInputs(std::span<const CookBrushGeometry> brushes, double cellSize)
    {
        uint64_t h = HashBytes64(std::as_bytes(std::span<const double>{ &cellSize, 1 }));
        for (const CookBrushGeometry& brush : brushes)
            for (const CookFace& face : brush.Faces)
            {
                h = HashBytes64(face.Material.Path, h);
                h = HashBytes64(std::as_bytes(std::span<const StaticMeshVertex>{
                    face.Triangles.data(), face.Triangles.size() }), h);
            }
        return h;
    }

    std::string CellName(const Vec3i& coord)
    {
        return "cell_" + std::to_string(coord.X) + "_" + std::to_string(coord.Y)
            + "_" + std::to_string(coord.Z) + ".smesh";
    }
} // namespace

JsonValue BuildCellEntity(const Vec3d& origin,
                          std::string_view meshPath,
                          std::span<const AssetRef> materials)
{
    JsonValue::Object local{
        { "position", JsonValue(JsonValue::Array{
            JsonValue(static_cast<double>(origin.X)),
            JsonValue(static_cast<double>(origin.Y)),
            JsonValue(static_cast<double>(origin.Z)) }) },
        { "rotation", JsonValue(JsonValue::Array{
            JsonValue(0.0), JsonValue(0.0), JsonValue(0.0), JsonValue(1.0) }) },
        { "scale", JsonValue(JsonValue::Array{
            JsonValue(1.0), JsonValue(1.0), JsonValue(1.0) }) },
    };

    JsonValue::Array materialPaths;
    materialPaths.reserve(materials.size());
    for (const AssetRef& material : materials)
        materialPaths.push_back(JsonValue(material.Path));

    JsonValue::Object staticMesh{
        { "mesh", JsonValue(std::string(meshPath)) },
        { "materials", JsonValue(std::move(materialPaths)) },
        { "visible", JsonValue(true) },
        { "layer_mask", JsonValue(static_cast<double>(0xFFFFFFFFu)) },
        { "section_mask", JsonValue(static_cast<double>(0xFFFFFFFFu)) },
    };

    return JsonValue(JsonValue::Object{
        { "components", JsonValue(JsonValue::Object{
            { "Transform", JsonValue(JsonValue::Object{ { "local", JsonValue(std::move(local)) } }) },
            { "StaticMesh", JsonValue(std::move(staticMesh)) },
        }) },
    });
}

namespace
{
// The cook kernel. Operates on a mutable document it is free to mutate
// destructively (it drops brush entities before serializing the passthrough
// scene), so both callers hand it a throwaway: the file cook loads one from
// disk, the live cook snapshots the editor's document. stem names the level's
// artifacts; sourceRel is the cooked-cache source key.
LevelCookResult CookLevelDocument(LevelDocument& doc,
                                  std::string_view stem,
                                  std::string_view sourceRel,
                                  const std::filesystem::path& assetsRoot,
                                  double cellSize)
{
    LevelCookResult result;

    // Collect -> hash -> cluster. The hash is taken on the collected input so it
    // reflects exactly what gets baked.
    std::vector<CookBrushGeometry> brushes = CollectCookBrushes(doc.GetScene(), doc.GetDefaultMaterial());
    const uint64_t geometryHash = HashBrushInputs(brushes, cellSize);
    std::vector<BrushCell> cells = ClusterBrushesIntoCells(brushes, cellSize);

    const std::string stemStr(stem);
    LoggingProvider logging;
    MeshSerializer serializer(logging);

    JsonValue::Array cellEntities;
    std::vector<CookedArtifact> artifacts;
    std::vector<std::string> materialRefs; // distinct face materials, in cook order
    std::unordered_set<std::string> seenMaterial;
    std::unordered_set<std::string> generatedMeshPaths;

    for (const BrushCell& cell : cells)
    {
        std::vector<AssetRef> order = CollectMaterialOrder(cell.Faces);

        MeshGeometry geometry;
        std::string bakeError;
        if (!BakeBrushFacesToStaticMesh(cell.Faces, order, geometry, &bakeError))
        {
            result.Error = "CookLevel: " + bakeError;
            return result;
        }

        const std::string cellName = CellName(cell.Coord);
        const std::string meshAssetPath = "asset://levels/" + stemStr + "/" + cellName;
        const std::string meshRelPath = ".cooked/levels/" + stemStr + "/" + cellName;
        const std::filesystem::path meshPhysical = assetsRoot / meshRelPath;

        std::error_code ec;
        std::filesystem::create_directories(meshPhysical.parent_path(), ec);
        if (!serializer.WriteToFile(meshPhysical.generic_string(), geometry))
        {
            result.Error = "CookLevel: could not write mesh '" + meshPhysical.generic_string() + "'";
            return result;
        }

        cellEntities.push_back(BuildCellEntity(cell.Origin, meshAssetPath, order));
        artifacts.push_back(CookedArtifact{ meshAssetPath, meshRelPath, AssetType::StaticMesh });
        result.GeneratedMeshPaths.push_back(meshAssetPath);
        generatedMeshPaths.insert(meshAssetPath);
        for (const AssetRef& material : order)
            if (seenMaterial.insert(material.Path).second)
                materialRefs.push_back(material.Path);
    }

    // Drop the brush entities so SaveSceneJson emits only passthrough game
    // components (the cook is the one and only StaticMeshComponent emitter), then
    // append the cell entities.
    {
        LevelScene& scene = doc.GetScene();
        std::vector<EntityId> brushEntities;
        for (EntityId entity : scene.GetAllEntities())
            if (scene.TryGetBrush(entity) != nullptr)
                brushEntities.push_back(entity);
        for (EntityId entity : brushEntities)
            scene.DestroyEntity(entity);
    }

    JsonValue cooked = SaveSceneJson(doc.GetRegistry());
    bool appended = false;
    if (cooked.IsObject())
        for (auto& [key, value] : cooked.AsObject())
            if (key == "entities" && value.IsArray())
            {
                for (JsonValue& cellEntity : cellEntities)
                    value.AsArray().push_back(std::move(cellEntity));
                appended = true;
                break;
            }
    if (!appended)
    {
        result.Error = "CookLevel: assembled scene has no entities array";
        return result;
    }

    std::error_code ec;
    const std::filesystem::path cookedDir = assetsRoot / ".cooked/levels";
    std::filesystem::create_directories(cookedDir, ec);
    const std::filesystem::path cookedScenePath = cookedDir / (stemStr + ".cooked.json");
    const std::filesystem::path manifestPath = cookedDir / (stemStr + ".manifest.json");

    // asset:// resolution: Generated cell meshes live under .cooked/; every other
    // ref (materials, their textures) is an authored asset under the root.
    const auto physicalPathFor =
        [&assetsRoot, &generatedMeshPaths](std::string_view assetPath) -> std::filesystem::path {
            constexpr std::string_view prefix = "asset://";
            const std::string rel(assetPath.substr(prefix.size()));
            if (generatedMeshPaths.count(std::string(assetPath)) != 0)
                return assetsRoot / ".cooked" / rel;
            return assetsRoot / rel;
        };

    std::string cookError;
    if (!WriteCookedScene(cooked, materialRefs, physicalPathFor,
            assetsRoot / kAssetIdMapFileName, manifestPath, cookedScenePath, &cookError))
    {
        result.Error = "CookLevel: " + cookError;
        return result;
    }

    // Record source -> artifacts (source key = caller-supplied rel path, hash key
    // = brush-geometry hash).
    const std::filesystem::path indexPath = assetsRoot / ".cooked/index.json";
    CookedCacheIndex index;
    (void)CookedCacheIndex::LoadFromFile(indexPath.generic_string(), index); // cold cache is fine
    CookedSourceEntry entry;
    entry.SourceRelPath = std::string(sourceRel);
    entry.SourceHash = geometryHash;
    entry.Artifacts = std::move(artifacts);
    index.Put(std::move(entry));
    (void)index.SaveToFile(indexPath.generic_string());

    result.Success = true;
    result.CookedScenePath = cookedScenePath;
    result.ManifestPath = manifestPath;
    result.CellCount = cells.size();
    return result;
}
} // namespace

LevelCookResult CookLevel(const std::filesystem::path& authoredLevelPath,
                          const std::filesystem::path& assetsRoot,
                          double cellSize)
{
    LevelDocument doc;
    if (!doc.Load(authoredLevelPath.generic_string()))
    {
        LevelCookResult result;
        result.Error = "CookLevel: could not load '" + authoredLevelPath.generic_string() + "'";
        return result;
    }

    std::error_code ec;
    const std::string sourceRel =
        std::filesystem::relative(authoredLevelPath, assetsRoot, ec).generic_string();
    return CookLevelDocument(doc, authoredLevelPath.stem().generic_string(), sourceRel,
                             assetsRoot, cellSize);
}

LevelCookResult CookLevel(const LevelDocument& liveDocument,
                          std::string_view levelName,
                          const std::filesystem::path& assetsRoot,
                          double cellSize)
{
    // Snapshot the live (possibly unsaved) document into a throwaway the kernel is
    // free to mutate, leaving the editor's document untouched.
    LevelDocument snapshot;
    if (!snapshot.LoadFromJson(liveDocument.ToJson()))
    {
        LevelCookResult result;
        result.Error = "CookLevel: could not snapshot the live document";
        return result;
    }

    const std::string sourceRel = "levels/" + std::string(levelName) + ".level.json";
    return CookLevelDocument(snapshot, levelName, sourceRel, assetsRoot, cellSize);
}
