#include "BrushGeometry.h"

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

BrushState BrushGeometry::Translate(const BrushState& state, Vec3d delta)
{
    BrushState translated = state;
    translated.Transform.Position += delta;
    return translated;
}
