#pragma once

#include "LevelScene.h"

#include <math/geometry/3d/Aabb3d.h>

#include <array>
#include <optional>

struct BrushState
{
    Transform3f Transform = Transform3f::Identity();
    Vec3d HalfExtents = { 0.5, 0.5, 0.5 };
};

struct BrushFaceGeometry
{
    int Axis = 0;
    float Sign = 1.0f;
    Vec3d Normal = {};
    float PlanePosition = 0.0f;
    Aabb3d Bounds = {};
};

class BrushGeometry
{
public:
    [[nodiscard]] static std::optional<BrushState> TryGetState(const LevelScene& scene, EntityId entity);
    static void ApplyState(LevelScene& scene, EntityId entity, const BrushState& state);

    [[nodiscard]] static Aabb3d ComputeBounds(const BrushState& state);
    [[nodiscard]] static std::array<Vec3d, 8> ComputeCorners(const BrushState& state);
    [[nodiscard]] static BrushFaceGeometry ComputeFaceGeometry(const BrushState& state,
                                                              int faceIndex,
                                                              float thickness = 0.0f);

    [[nodiscard]] static BrushState Translate(const BrushState& state, Vec3d delta);
    [[nodiscard]] static BrushState ResizeFace(const BrushState& state,
                                               int faceIndex,
                                               float facePosition,
                                               float minHalfExtent);
};
