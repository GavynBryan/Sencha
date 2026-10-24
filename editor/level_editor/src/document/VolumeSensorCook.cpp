#include "document/VolumeSensorCook.h"

#include "brush/BrushBoxFit.h"
#include "brush/BrushEvaluation.h"
#include "document/EditorScene.h"

#include <format>
#include <vector>

std::optional<Collider> CookVolumeSensor(const EditorScene& scene, EntityId entity, std::string& reason)
{
    const BrushEvaluated* evaluated = scene.TryGetBrushPieces(entity, BrushEvaluationPolicy::Cook());
    const Transform3f* world = scene.TryGetWorldTransform(entity);
    if (evaluated == nullptr || world == nullptr)
    {
        reason = "it has no brush geometry";
        return std::nullopt;
    }
    if (evaluated->Pieces.size() != 1)
    {
        reason = std::format("its modifiers make {} pieces; a sensor is one box", evaluated->Pieces.size());
        return std::nullopt;
    }
    const BrushPiece& piece = evaluated->Pieces.front();
    const BrushMesh* mesh = evaluated->Meshes[piece.MeshIndex].Mesh;
    if (mesh == nullptr)
    {
        reason = "it has no brush geometry";
        return std::nullopt;
    }

    // The body takes the entity's position and turn and no scale, so the box is
    // fitted where the scaled, placed corners land in that frame.
    const Transform3f placed = PieceWorldTransform(*world, piece);
    const Quatf unturn = world->Rotation.Inverse();
    std::vector<Vec3d> corners;
    corners.reserve(mesh->Vertices.size());
    for (const BrushVertex& vertex : mesh->Vertices)
        corners.push_back(unturn.RotateVector(placed.TransformPoint(vertex.Position) - world->Position));

    const std::optional<OrientedBox> box = FitOrientedBox(*mesh, corners, reason);
    if (!box)
        return std::nullopt;
    Collider collider;
    collider.Shape = CollisionShape::MakeBox(box->HalfExtents);
    collider.Shape.Center = box->Center;
    collider.Shape.Orientation = box->Orientation;
    collider.IsTrigger = true;
    return collider;
}
