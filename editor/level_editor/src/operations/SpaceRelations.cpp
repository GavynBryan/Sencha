#include "SpaceRelations.h"

#include "brush/ShellGeneration.h"
#include "document/EditorScene.h"
#include "document/SpaceComponents.h"

#include <world/registry/Registry.h>
#include <world/transform/TransformComponents.h>

#include <algorithm>
#include <cmath>

bool WorldFace::Over(const Vec3d& point) const
{
    const Vec3d onPlane = point - Normal * Normal.Dot(point - Corners.front());
    for (std::size_t i = 0; i < Corners.size(); ++i)
        if ((Corners[(i + 1) % Corners.size()] - Corners[i]).Cross(onPlane - Corners[i]).Dot(Normal) < -1e-3f)
            return false;
    return true;
}

std::optional<WorldFace> WorldFaceOf(const EditorScene& scene, EntityId entity, std::uint32_t face)
{
    const BrushMesh* mesh = scene.TryGetBrushMesh(entity);
    const Transform3f* world = scene.TryGetWorldTransform(entity);
    if (mesh == nullptr || world == nullptr || face >= mesh->Faces.size())
        return std::nullopt;
    WorldFace placed;
    for (const std::uint32_t v : mesh->Faces[face].Loop)
        placed.Corners.push_back(world->TransformPoint(mesh->Vertices[v].Position));
    Vec3d normal{ 0, 0, 0 };
    for (std::size_t i = 0; i < placed.Corners.size(); ++i)
        normal = normal + placed.Corners[i].Cross(placed.Corners[(i + 1) % placed.Corners.size()]);
    placed.Normal = normal.Normalized();
    return placed;
}

namespace
{
    bool IsSpace(const EditorScene& scene, EntityId entity)
    {
        return scene.RoleOf(entity) == BrushRole::Volume && scene.GetRegistry().Components.HasComponent<Space>(entity);
    }

    std::uint32_t FaceCount(const EditorScene& scene, EntityId entity)
    {
        const BrushMesh* mesh = scene.TryGetBrushMesh(entity);
        return mesh != nullptr ? static_cast<std::uint32_t>(mesh->Faces.size()) : 0;
    }
}

std::optional<WallHit> WallAt(const EditorScene& scene, const Vec3d& point)
{
    std::optional<WallHit> best;
    float bestOff = 0.0f;
    for (const EntityId entity : scene.GetAllEntities())
    {
        const Shell* shell = scene.GetRegistry().Components.TryGet<Shell>(entity);
        if (shell == nullptr || !IsSpace(scene, entity))
            continue;
        for (std::uint32_t f = 0; f < FaceCount(scene, entity); ++f)
        {
            const std::optional<WorldFace> face = WorldFaceOf(scene, entity, f);
            if (!face || std::abs(face->Normal.Y) > 0.7f)
                continue;
            const float depth = face->Normal.Dot(point - face->Corners.front());
            if (depth < -kOpeningReach || depth > shell->Thickness + kOpeningReach || !face->Over(point))
                continue;
            const float off = std::abs(depth - 0.5f * shell->Thickness);
            if (best && off >= bestOff)
                continue;
            float floorY = face->Corners.front().Y;
            for (const Vec3d& corner : face->Corners)
                floorY = std::min(floorY, corner.Y);
            best = WallHit{ entity, face->Normal, point + face->Normal * (0.5f * shell->Thickness - depth), floorY };
            bestOff = off;
        }
    }
    return best;
}

std::optional<FarSide> FarSideOf(const EditorScene& scene, EntityId owner, EntityId opening)
{
    const Transform3f* frame = scene.TryGetWorldTransform(opening);
    const Opening* cut = scene.GetRegistry().Components.TryGet<Opening>(opening);
    if (frame == nullptr || cut == nullptr)
        return std::nullopt;
    const Vec3d through = frame->Rotation.RotateVector(Vec3d{ 0, 0, 1 });
    const Vec3d middle = frame->Position + frame->Rotation.RotateVector(Vec3d{ 0, 0.5f * cut->Height, 0 });
    for (const EntityId entity : scene.GetAllEntities())
    {
        if (entity == owner || !IsSpace(scene, entity))
            continue;
        for (std::uint32_t f = 0; f < FaceCount(scene, entity); ++f)
        {
            const std::optional<WorldFace> face = WorldFaceOf(scene, entity, f);
            if (!face || face->Normal.Dot(through) > -0.99f)
                continue;
            const float depth = face->Normal.Dot(middle - face->Corners.front());
            if (std::abs(depth) <= kOpeningReach && face->Over(middle))
                return FarSide{ entity, f, face->Normal };
        }
    }
    return std::nullopt;
}

std::optional<EntityId> OwnerOf(const EditorScene& scene, EntityId opening)
{
    if (const Parent* parent = scene.GetRegistry().Components.TryGet<Parent>(opening);
        parent != nullptr && IsSpace(scene, parent->Entity))
        return parent->Entity;
    const Transform3f* frame = scene.TryGetWorldTransform(opening);
    if (frame == nullptr)
        return std::nullopt;
    const std::optional<WallHit> wall = WallAt(scene, frame->Position + Vec3d{ 0, 0.01f, 0 });
    return wall ? std::optional<EntityId>(wall->Space) : std::nullopt;
}
