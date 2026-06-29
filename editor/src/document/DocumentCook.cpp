#include "DocumentCook.h"

#include "BrushCookInput.h"
#include "EditorDocument.h"

#include <assets/cook/BrushClustering.h>
#include <assets/cook/BrushGeometryCook.h>
#include <assets/cook/CollisionShapeCook.h>
#include <assets/cook/CookedCache.h>
#include <assets/cook/ImportOnDemand.h>
#include <assets/cook/SceneCookOutput.h>
#include <assets/cook/TextureCook.h>
#include <assets/static_mesh/MeshSerializer.h>
#include <core/assets/AssetIdMap.h>
#include <core/assets/AssetRegistry.h>
#include <core/json/JsonStringify.h>
#include <core/json/JsonValue.h>
#include <core/assets/RuntimeAssets.h>
#include <core/hash/ContentHash.h>
#include <core/logging/LoggingProvider.h>
#include <render/static_mesh/MeshGeometry.h>
#include <world/registry/Registry.h>
#include <world/serialization/SceneSerializationContext.h>
#include <world/serialization/SceneSerializer.h>

#include <cstddef>
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

    std::string CellBase(const Vec3i& coord)
    {
        return "cell_" + std::to_string(coord.X) + "_" + std::to_string(coord.Y)
            + "_" + std::to_string(coord.Z);
    }

    std::string CellName(const Vec3i& coord) { return CellBase(coord) + ".smesh"; }

    // Flatten a cell's already-triangulated faces into a position/index soup for
    // the collision bake (cell-local, the same triangles the render mesh uses).
    void CollectCellTriangles(const std::vector<CookFace>& faces,
                              std::vector<Vec3d>& positions,
                              std::vector<uint32_t>& indices)
    {
        for (const CookFace& face : faces)
            for (const StaticMeshVertex& vertex : face.Triangles)
            {
                indices.push_back(static_cast<uint32_t>(positions.size()));
                positions.push_back(vertex.Position);
            }
    }

    // One cell's cooked collision: the blob's path (relative to the cooked root)
    // and the cell origin the runtime places the static collider at.
    struct CollisionEntry
    {
        std::string BlobRelPath;
        Vec3d Origin;
    };
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
DocumentCookResult CookDocumentKernel(EditorDocument& doc,
                                  std::string_view stem,
                                  std::string_view sourceRel,
                                  const std::filesystem::path& assetsRoot,
                                  double cellSize,
                                  LoggingProvider& logging,
                                  AssetSystem* assetSystem)
{
    DocumentCookResult result;

    // Collect -> hash -> cluster. The hash is taken on the collected input so it
    // reflects exactly what gets baked.
    std::vector<CookBrushGeometry> brushes = CollectCookBrushes(doc.GetScene(), doc.GetDefaultMaterial());
    const uint64_t geometryHash = HashBrushInputs(brushes, cellSize);
    std::vector<BrushCell> cells = ClusterBrushesIntoCells(brushes, cellSize);

    const std::string stemStr(stem);
    MeshSerializer serializer(logging);

    JsonValue::Array cellEntities;
    std::vector<CookedArtifact> artifacts;
    std::vector<std::string> materialRefs; // distinct face materials, in cook order
    std::unordered_set<std::string> seenMaterial;
    std::unordered_set<std::string> generatedMeshPaths;
    std::vector<CollisionEntry> collisionEntries;

    for (const BrushCell& cell : cells)
    {
        std::vector<AssetRef> order = CollectMaterialOrder(cell.Faces);

        MeshGeometry geometry;
        std::string bakeError;
        if (!BakeBrushFacesToStaticMesh(cell.Faces, order, geometry, &bakeError))
        {
            result.Error = "CookDocument: " + bakeError;
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
            result.Error = "CookDocument: could not write mesh '" + meshPhysical.generic_string() + "'";
            return result;
        }

        cellEntities.push_back(BuildCellEntity(cell.Origin, meshAssetPath, order));
        artifacts.push_back(CookedArtifact{ meshAssetPath, meshRelPath, AssetType::StaticMesh });
        result.GeneratedMeshPaths.push_back(meshAssetPath);
        generatedMeshPaths.insert(meshAssetPath);
        for (const AssetRef& material : order)
            if (seenMaterial.insert(material.Path).second)
                materialRefs.push_back(material.Path);

        // Collision: bake the same cell triangles into a pre-baked Jolt blob, a
        // sibling of the cell mesh. Authored brushes become collidable with no
        // collider authoring; the runtime loads these from the sidecar at map load.
        std::vector<Vec3d> collisionPositions;
        std::vector<uint32_t> collisionIndices;
        CollectCellTriangles(cell.Faces, collisionPositions, collisionIndices);
        const std::vector<std::byte> collisionBlob =
            BakeCollisionBlob(collisionPositions, collisionIndices);
        if (!collisionBlob.empty())
        {
            const std::string colRel = "levels/" + stemStr + "/" + CellBase(cell.Coord) + ".scol";
            const std::filesystem::path colPhysical = assetsRoot / ".cooked" / colRel;
            std::ofstream colFile(colPhysical, std::ios::binary);
            colFile.write(reinterpret_cast<const char*>(collisionBlob.data()),
                          static_cast<std::streamsize>(collisionBlob.size()));
            if (colFile.good())
            {
                collisionEntries.push_back(CollisionEntry{ colRel, cell.Origin });
                artifacts.push_back(
                    CookedArtifact{ "asset://" + colRel, ".cooked/" + colRel, AssetType::Collision });
            }
        }
    }

    // Drop the brush entities so SaveSceneJson emits only passthrough game
    // components (the cook is the one and only StaticMeshComponent emitter), then
    // append the cell entities.
    {
        EditorScene& scene = doc.GetScene();
        std::vector<EntityId> brushEntities;
        for (EntityId entity : scene.GetAllEntities())
            if (scene.TryGetBrush(entity) != nullptr)
                brushEntities.push_back(entity);
        for (EntityId entity : brushEntities)
            scene.DestroyEntity(entity);
    }

    // Serialize passthrough entities through the shared asset system so prop
    // StaticMesh handles emit asset:// paths (the cell entities are raw JSON and
    // bypass the codec, but authored props go through it). A null assetSystem is
    // the brush-only cook (no asset fields to resolve).
    SceneSerializationContext context(logging, assetSystem);
    JsonValue cooked = SaveSceneJson(doc.GetRegistry(), context);
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
        result.Error = "CookDocument: assembled scene has no entities array";
        return result;
    }

    std::error_code ec;
    const std::filesystem::path cookedDir = assetsRoot / ".cooked/levels";
    std::filesystem::create_directories(cookedDir, ec);
    const std::filesystem::path cookedScenePath = cookedDir / (stemStr + ".cooked.json");
    const std::filesystem::path manifestPath = cookedDir / (stemStr + ".manifest.json");

    // Collision sidecar: the runtime loads this at map load (LoadZoneCollision)
    // to spawn the level's static brush colliders. Empty array if no brushes.
    {
        JsonValue::Array sidecar;
        sidecar.reserve(collisionEntries.size());
        for (const CollisionEntry& entry : collisionEntries)
            sidecar.push_back(JsonValue(JsonValue::Object{
                { "blob", JsonValue(entry.BlobRelPath) },
                { "origin", JsonValue(JsonValue::Array{
                    JsonValue(static_cast<double>(entry.Origin.X)),
                    JsonValue(static_cast<double>(entry.Origin.Y)),
                    JsonValue(static_cast<double>(entry.Origin.Z)) }) },
            }));
        std::ofstream sidecarFile(cookedDir / (stemStr + ".collision.json"));
        sidecarFile << JsonStringify(JsonValue(std::move(sidecar)), /*pretty*/ true);
    }

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
        result.Error = "CookDocument: " + cookError;
        return result;
    }

    // Cook source textures the level's materials reference (.png -> .stex) into
    // .cooked/ so the COOK=OFF player can load them. The import driver maintains
    // the cooked index keyed by source path; the index.Put below loads that
    // updated index and adds the level entry, so both survive. Idempotent:
    // unchanged sources are served from the cooked cache.
    {
        PngTextureImporter textureImporter;
        AssetImporterRegistry importers;
        importers.Register(textureImporter);
        AssetRegistry scratch(logging); // we want the on-disk artifacts + index, not a live registry
        (void)ImportAssetsOnDemand(assetsRoot.generic_string(), importers, scratch, logging);
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

DocumentCookResult CookDocument(const std::filesystem::path& authoredLevelPath,
                          const std::filesystem::path& assetsRoot,
                          double cellSize)
{
    // Headless, brush-only: a sink-less logger discards everything, no assets.
    LoggingProvider logging;
    EditorDocument doc(logging);
    if (!doc.Load(authoredLevelPath.generic_string()))
    {
        DocumentCookResult result;
        result.Error = "CookDocument: could not load '" + authoredLevelPath.generic_string() + "'";
        return result;
    }

    std::error_code ec;
    const std::string sourceRel =
        std::filesystem::relative(authoredLevelPath, assetsRoot, ec).generic_string();
    return CookDocumentKernel(doc, authoredLevelPath.stem().generic_string(), sourceRel,
                             assetsRoot, cellSize, logging, nullptr);
}

DocumentCookResult CookDocument(const EditorDocument& liveDocument,
                          std::string_view levelName,
                          const std::filesystem::path& assetsRoot,
                          double cellSize,
                          LoggingProvider& logging,
                          RuntimeAssets* assets)
{
    // Snapshot the live (possibly unsaved) document into a throwaway the kernel is
    // free to mutate, leaving the editor's document untouched. The snapshot shares
    // the same asset system so its prop handles round-trip through ToJson/LoadFromJson.
    EditorDocument snapshot(logging);
    if (assets != nullptr)
        snapshot.SetAssetEnvironment(*assets);
    if (!snapshot.LoadFromJson(liveDocument.ToJson()))
    {
        DocumentCookResult result;
        result.Error = "CookDocument: could not snapshot the live document";
        return result;
    }

    const std::string sourceRel = "levels/" + std::string(levelName) + ".level.json";
    return CookDocumentKernel(snapshot, levelName, sourceRel, assetsRoot, cellSize,
                             logging, assets != nullptr ? &assets->Assets : nullptr);
}
