#include "BrushGeometry.h"

#include <cstdint>

namespace
{
    // Newell's method over world-space corners — robust outward normal.
    Vec3d WorldFaceNormal(const std::vector<Vec3d>& corners)
    {
        Vec3d normal{ 0.0f, 0.0f, 0.0f };
        const std::size_t n = corners.size();
        if (n < 3)
            return normal;
        for (std::size_t i = 0; i < n; ++i)
        {
            const Vec3d& a = corners[i];
            const Vec3d& b = corners[(i + 1) % n];
            normal.X += (a.Y - b.Y) * (a.Z + b.Z);
            normal.Y += (a.Z - b.Z) * (a.X + b.X);
            normal.Z += (a.X - b.X) * (a.Y + b.Y);
        }
        return normal.SqrMagnitude() > 0.0f ? normal.Normalized() : Vec3d{ 0.0f, 0.0f, 0.0f };
    }

    BrushFaceDescriptor BuildFaceDescriptor(const LevelScene& scene, EntityId entity,
                                            std::uint32_t faceIndex,
                                            const Transform3f& transform, const BrushMesh& mesh)
    {
        BrushFaceDescriptor desc;
        desc.Ref = SelectableRef::BrushFaceSelection(scene.GetRegistry().Id, entity, faceIndex);
        desc.Geometry.FaceIndex = faceIndex;

        const BrushFace& face = mesh.Faces[faceIndex];
        desc.Geometry.Corners.reserve(face.Loop.size());
        Vec3d center{ 0.0f, 0.0f, 0.0f };
        for (std::uint32_t index : face.Loop)
        {
            const Vec3d world = transform.TransformPoint(mesh.Vertices[index].Position);
            desc.Geometry.Corners.push_back(world);
            center += world;
        }
        if (!desc.Geometry.Corners.empty())
            center = center * (1.0f / static_cast<float>(desc.Geometry.Corners.size()));
        desc.Geometry.Center = center;
        desc.Geometry.Normal = WorldFaceNormal(desc.Geometry.Corners);
        return desc;
    }
}

std::optional<BrushState> BrushGeometry::TryGetState(const LevelScene& scene, EntityId entity)
{
    const Transform3f* transform = scene.TryGetTransform(entity);
    const BrushMesh* mesh = scene.TryGetBrushMesh(entity);
    if (transform == nullptr || mesh == nullptr)
        return std::nullopt;

    // Box view derived from the mesh's local bounds, for the body bounds/move and
    // the create-drag preview.
    return BrushState{
        .Transform = *transform,
        .HalfExtents = BrushComputeBounds(*mesh).HalfExtent(),
    };
}

Aabb3d BrushGeometry::ComputeBounds(const BrushState& state)
{
    return Aabb3d::FromCenterHalfExtent(state.Transform.Position, state.HalfExtents);
}

std::array<Vec3d, 8> BrushGeometry::ComputeCorners(const BrushState& state)
{
    return {
        state.Transform.TransformPoint(Vec3d(-state.HalfExtents.X, -state.HalfExtents.Y, -state.HalfExtents.Z)),
        state.Transform.TransformPoint(Vec3d(state.HalfExtents.X, -state.HalfExtents.Y, -state.HalfExtents.Z)),
        state.Transform.TransformPoint(Vec3d(state.HalfExtents.X, state.HalfExtents.Y, -state.HalfExtents.Z)),
        state.Transform.TransformPoint(Vec3d(-state.HalfExtents.X, state.HalfExtents.Y, -state.HalfExtents.Z)),
        state.Transform.TransformPoint(Vec3d(-state.HalfExtents.X, -state.HalfExtents.Y, state.HalfExtents.Z)),
        state.Transform.TransformPoint(Vec3d(state.HalfExtents.X, -state.HalfExtents.Y, state.HalfExtents.Z)),
        state.Transform.TransformPoint(Vec3d(state.HalfExtents.X, state.HalfExtents.Y, state.HalfExtents.Z)),
        state.Transform.TransformPoint(Vec3d(-state.HalfExtents.X, state.HalfExtents.Y, state.HalfExtents.Z)),
    };
}

std::vector<BrushFaceDescriptor> BrushGeometry::EnumerateFaces(const LevelScene& scene, EntityId entity)
{
    std::vector<BrushFaceDescriptor> faces;
    const Transform3f* transform = scene.TryGetTransform(entity);
    const BrushMesh* mesh = scene.TryGetBrushMesh(entity);
    if (transform == nullptr || mesh == nullptr)
        return faces;

    faces.reserve(mesh->Faces.size());
    for (std::uint32_t i = 0; i < mesh->Faces.size(); ++i)
        faces.push_back(BuildFaceDescriptor(scene, entity, i, *transform, *mesh));
    return faces;
}

std::optional<BrushFaceDescriptor> BrushGeometry::TryGetFace(const LevelScene& scene, const SelectableRef& ref)
{
    if (!ref.IsBrushFace() || ref.Registry != scene.GetRegistry().Id)
        return std::nullopt;

    const Transform3f* transform = scene.TryGetTransform(ref.Entity);
    const BrushMesh* mesh = scene.TryGetBrushMesh(ref.Entity);
    if (transform == nullptr || mesh == nullptr || ref.ElementId >= mesh->Faces.size())
        return std::nullopt;

    return BuildFaceDescriptor(scene, ref.Entity, ref.ElementId, *transform, *mesh);
}

BrushState BrushGeometry::Translate(const BrushState& state, Vec3d delta)
{
    BrushState translated = state;
    translated.Transform.Position += delta;
    return translated;
}
