#include "CellArtifactCook.h"

#include "CookArtifactTransaction.h"
#include "CookStepProgress.h"
#include "DocumentArtifactCatalog.h"

#include <assets/cook/BrushGeometryCook.h>
#include <assets/cook/CollisionShapeCook.h>
#include <core/assets/AssetRef.h>
#include <project/CookProfile.h>

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <system_error>

namespace
{
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

bool EmitCellArtifacts(const std::vector<BrushCell>& cells,
                       const std::filesystem::path& assetsRoot,
                       std::string_view stem,
                       bool emitCollision,
                       CookArtifactTransaction& transaction,
                       DocumentArtifactCatalog& catalog,
                       CookStepProgress& progress,
                       std::vector<PendingCellMesh>& meshes,
                       JsonValue::Array& entities,
                       std::vector<CellCollisionEntry>& collision,
                       DocumentCookResult& result)
{
    const std::string stemStr(stem);
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
        const std::string meshAssetPath = "asset://levels/" + stemStr + "/" + cellName;
        const std::string meshRelPath = ".cooked/levels/" + stemStr + "/" + cellName;
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
                    "levels/" + stemStr + "/" + CellBase(cell.Coord) + ".scol";
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
        progress.Complete();
    return true;
}
