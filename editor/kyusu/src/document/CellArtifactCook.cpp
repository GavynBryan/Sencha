#include "CellArtifactCook.h"

#include "CookArtifactTransaction.h"
#include "CookStepProgress.h"
#include "DocumentArtifactCatalog.h"
#include "DocumentCookContext.h"

#include <assets/cook/BrushGeometryCook.h>
#include <assets/cook/CollisionShapeCook.h>
#include <core/assets/AssetRef.h>
#include <project/CookProfile.h>

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <span>
#include <system_error>

namespace
{
    std::string CellBase(const Vec3i& coord)
    {
        return "cell_" + std::to_string(coord.X) + "_" + std::to_string(coord.Y)
            + "_" + std::to_string(coord.Z);
    }

    std::string CellName(const Vec3i& coord) { return CellBase(coord) + ".smesh"; }

    // The cooked StaticMesh entity JSON for one cell: a Transform at the cell
    // origin plus a StaticMesh referencing the cell mesh and its per-section
    // materials (bare asset:// paths; WriteCookedScene stamps the ones the id
    // map knows).
    JsonValue BuildCellEntity(const Vec3d& origin, std::string_view meshPath,
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

    // Flatten a cell's already-triangulated faces into a position/index soup for
    // the collision bake (cell-local, the same triangles the render mesh uses).
    void CollectCellTriangles(const std::vector<CookFace>& faces,
                              std::vector<Vec3d>& positions,
                              std::vector<std::uint32_t>& indices)
    {
        for (const CookFace& face : faces)
            for (const StaticMeshVertex& vertex : face.Triangles)
            {
                indices.push_back(static_cast<std::uint32_t>(positions.size()));
                positions.push_back(vertex.Position);
            }
    }
} // namespace

bool EmitCellArtifacts(const DocumentCookContext& ctx,
                       const std::vector<BrushCell>& cells,
                       bool emitCollision,
                       std::vector<PendingCellMesh>& meshes,
                       JsonValue::Array& entities,
                       std::vector<CellCollisionEntry>& collision)
{
    const std::filesystem::path& assetsRoot = ctx.AssetsRoot;
    CookArtifactTransaction& transaction = ctx.Transaction;
    DocumentArtifactCatalog& catalog = ctx.Catalog;
    CookStepProgress& progress = ctx.Progress;
    DocumentCookResult& result = ctx.Result;
    const std::string stemStr(ctx.Stem());
    progress.Begin(CookStepIds::RenderMeshes);
    for (const BrushCell& cell : cells)
    {
        if (progress.Cancelled(result))
            return false;
        std::vector<AssetRef> order = CollectMaterialOrder(cell.Faces);

        MeshGeometry geometry;
        std::string bakeError;
        if (!BakeBrushFacesToStaticMesh(cell.Faces, order, geometry, &bakeError))
        {
            result.Error = "CookDocument: " + bakeError;
            return false;
        }

        const std::string cellName = CellName(cell.Coord);
        const std::string meshAssetPath = "asset://" + stemStr + "/" + cellName;
        const std::string meshRelPath = ".cooked/" + stemStr + "/" + cellName;
        const std::filesystem::path meshPhysical = assetsRoot / meshRelPath;
        const std::filesystem::path meshStaged = transaction.Stage(meshPhysical);

        std::error_code ec;
        std::filesystem::create_directories(meshStaged.parent_path(), ec);
        meshes.push_back(PendingCellMesh{ meshStaged, std::move(geometry), cell.Origin });

        entities.push_back(BuildCellEntity(cell.Origin, meshAssetPath, order));
        catalog.AddMesh(meshAssetPath, meshRelPath);
        result.GeneratedMeshPaths.push_back(meshAssetPath);
        for (const AssetRef& material : order)
            catalog.AddMaterial(material.Path);

        // Collision: bake the same cell triangles into a pre-baked Jolt blob, a
        // sibling of the cell mesh. Authored brushes become collidable with no
        // collider authoring; the runtime loads these from the sidecar at map load.
        if (emitCollision)
        {
            std::vector<Vec3d> collisionPositions;
            std::vector<std::uint32_t> collisionIndices;
            CollectCellTriangles(cell.Faces, collisionPositions, collisionIndices);
            const std::vector<std::byte> collisionBlob =
                BakeCollisionBlob(collisionPositions, collisionIndices);
            if (!collisionBlob.empty())
            {
                const std::string colRel =
                    stemStr + "/" + CellBase(cell.Coord) + ".scol";
                const std::filesystem::path colPhysical = assetsRoot / ".cooked" / colRel;
                const std::filesystem::path colStaged = transaction.Stage(colPhysical);
                std::ofstream colFile(colStaged, std::ios::binary);
                colFile.write(reinterpret_cast<const char*>(collisionBlob.data()),
                              static_cast<std::streamsize>(collisionBlob.size()));
                if (colFile.good())
                {
                    collision.push_back(CellCollisionEntry{ colRel, cell.Origin });
                    catalog.AddCollision("asset://" + colRel, ".cooked/" + colRel);
                }
            }
        }
    }
    progress.Complete();
    if (emitCollision)
    {
        progress.Begin(CookStepIds::Collision);
        progress.Complete();
    }
    return true;
}
