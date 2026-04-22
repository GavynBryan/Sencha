#include "BrushGeometry.h"

#include <algorithm>
#include <cmath>

std::optional<BrushState> BrushGeometry::TryGetState(const LevelScene& scene, EntityId entity)
{
    const Transform3f* transform = scene.TryGetTransform(entity);
    const BrushComponent* brush = scene.TryGetBrush(entity);
    if (transform == nullptr || brush == nullptr)
        return std::nullopt;

    return BrushState{
        .Transform = *transform,
        .HalfExtents = brush->HalfExtents,
    };
}

void BrushGeometry::ApplyState(LevelScene& scene, EntityId entity, const BrushState& state)
{
    scene.SetTransform(entity, state.Transform);
    scene.SetBrushHalfExtents(entity, state.HalfExtents);
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

BrushFaceGeometry BrushGeometry::ComputeFaceGeometry(const BrushState& state, int faceIndex, float thickness)
{
    BrushFaceGeometry face{};
    face.Axis = faceIndex / 2;
    face.Sign = (faceIndex % 2 == 0) ? 1.0f : -1.0f;
    face.Normal[face.Axis] = face.Sign;
    face.PlanePosition = state.Transform.Position[face.Axis] + face.Sign * state.HalfExtents[face.Axis];
    face.Bounds = ComputeBounds(state);
    face.Bounds.Min[face.Axis] = face.PlanePosition - thickness;
    face.Bounds.Max[face.Axis] = face.PlanePosition + thickness;
    return face;
}

BrushState BrushGeometry::Translate(const BrushState& state, Vec3d delta)
{
    BrushState translated = state;
    translated.Transform.Position += delta;
    return translated;
}

BrushState BrushGeometry::ResizeFace(const BrushState& state,
                                     int faceIndex,
                                     float facePosition,
                                     float minHalfExtent)
{
    BrushState resized = state;
    const int axis = faceIndex / 2;
    const float sign = (faceIndex % 2 == 0) ? 1.0f : -1.0f;
    const float fixedFacePos = state.Transform.Position[axis] - sign * state.HalfExtents[axis];
    resized.Transform.Position[axis] = (facePosition + fixedFacePos) * 0.5f;
    resized.HalfExtents[axis] = std::max(std::abs(facePosition - fixedFacePos) * 0.5f, minHalfExtent);
    return resized;
}
